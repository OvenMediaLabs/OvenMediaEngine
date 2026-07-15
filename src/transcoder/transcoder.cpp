#include <utility>

//==============================================================================
//
//  Transcoder
//
//  Created by Kwon Keuk Han
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================

#include <unistd.h>

#include <iostream>

#include "config/config_manager.h"
#include "transcoder.h"
#include "transcoder_gpu.h"
#include "transcoder_whisper_model_registry.h"
#include "transcoder_private.h"

std::shared_ptr<Transcoder> Transcoder::Create(std::shared_ptr<MediaRouterInterface> router)
{
	auto transcoder = std::make_shared<Transcoder>(router);
	if (!transcoder->Start())
	{
		logte("An error occurred while creating Transcoder");
		return nullptr;
	}
	return transcoder;
}

Transcoder::Transcoder(std::shared_ptr<MediaRouterInterface> router)
{
	_router = std::move(router);
}

bool Transcoder::Start()
{
	logtt("Transcoder has been started");

	SetModuleAvailable(true);

	TranscodeGPU::GetInstance()->Initialize();

	{
		auto &whisper_cfg = cfg::ConfigManager::GetInstance()->GetServer()->GetModules().GetWhisper();
		const auto &config_path = cfg::ConfigManager::GetInstance()->GetConfigPath();

		std::vector<std::pair<ov::String, std::vector<int32_t>>> preload_models;

		// A <Devices> token must be a plain non-negative integer (an OME device index).
		auto is_numeric = [](const ov::String &value) -> bool {
			if (value.IsEmpty())
			{
				return false;
			}
			for (const char *p = value.CStr(); *p != '\0'; ++p)
			{
				if (*p < '0' || *p > '9')
				{
					return false;
				}
			}
			return true;
		};

		for (const auto &entry : whisper_cfg.GetPreloadModels())
		{
			ov::String resolved = ov::GetFilePath(entry.GetPath(), config_path);

			// Parse <Devices> as OME device indices (the same namespace as
			// <Modules>nv:N), then map each to its CUDA device id. This keeps the
			// preloaded context and the per-stream STT encoder on the same GPU,
			// since OME and CUDA device ordering can differ (e.g. CUDA orders by
			// performance, OME by PCI bus).
			// - Omitted/empty → OME device 0 (default)
			// - "all" → load on every available GPU
			// - "0,1" etc → specific OME device indices
			const ov::String devices_str = entry.GetDevices().Trim();
			const bool load_all = devices_str.LowerCaseString() == "all";

			std::vector<int32_t> device_ids;
			if (devices_str.IsEmpty())
			{
				// Default: OME device 0 (GetExternalDeviceId returns -1 if unavailable).
				int32_t cuda_id = TranscodeGPU::GetInstance()->GetExternalDeviceId(cmn::MediaCodecModuleId::NVENC, 0);
				if (cuda_id >= 0)
				{
					device_ids.push_back(cuda_id);
				}
			}
			else if (load_all == false)
			{
				for (const auto &token : devices_str.Split(","))
				{
					ov::String trimmed = token.Trim();
					if (trimmed.IsEmpty())
					{
						// e.g. a trailing comma in "0,1," — ignore quietly.
						continue;
					}
					if (is_numeric(trimmed) == false || trimmed.GetLength() > 9)
					{
						// Non-numeric, or too many digits to be a valid int32 device index.
						logtw("Whisper preload: ignoring invalid device id \"%s\" in Devices(\"%s\"). path=%s", trimmed.CStr(), devices_str.CStr(), resolved.CStr());
						continue;
					}

					int32_t cuda_id = TranscodeGPU::GetInstance()->GetExternalDeviceId(
						cmn::MediaCodecModuleId::NVENC, ov::Converter::ToInt32(trimmed));
					if (cuda_id < 0)
					{
						logtw("Whisper preload: OME device id %s is not an available NVIDIA device, skipping. path=%s", trimmed.CStr(), resolved.CStr());
						continue;
					}
					device_ids.push_back(cuda_id);
				}
			}

			// Only "all" loads on every GPU (empty device_ids). If a specific or
			// default selection resolved nothing, skip the model rather than letting
			// an empty list fall through to "all".
			if (load_all == false && device_ids.empty())
			{
				logtw("Whisper preload: no usable GPU resolved from Devices(\"%s\"), skipping model. path=%s", devices_str.CStr(), resolved.CStr());
				continue;
			}

			preload_models.emplace_back(std::move(resolved), std::move(device_ids));
		}
		WhisperModelRegistry::GetInstance()->Preload(preload_models);
	}

	return true;
}

bool Transcoder::Stop()
{
	logtt("Transcoder has been stopped");

	WhisperModelRegistry::GetInstance()->Uninitialize();
	TranscodeGPU::GetInstance()->Uninitialize();

	return true;
}

bool Transcoder::OnCreateHost(const info::Host &host_info)
{
	return true;
}

bool Transcoder::OnDeleteHost(const info::Host &host_info)
{
	return true;
}

// Create Application
bool Transcoder::OnCreateApplication(const info::Application &app_info)
{
	auto application_id = app_info.GetId();

	auto application = TranscodeApplication::Create(app_info);
	if(application == nullptr)
	{
		logte("Could not create the transcoder application. [%s]", app_info.GetVHostAppName().CStr());
		return false;
	}

	{
		ov::LockGuard lock(_transcode_apps_mutex);
		_transcode_apps[application_id] = application;
	}

	// Register to MediaRouter
	if (_router->RegisterObserverApp(app_info, application) == false)
	{
		logte("Could not register transcoder application to mediarouter. [%s]", app_info.GetVHostAppName().CStr());

		ov::LockGuard lock(_transcode_apps_mutex);
		_transcode_apps.erase(application_id);
		return false;
	}

	// Register to MediaRouter
	if (_router->RegisterConnectorApp(app_info, application) == false)
	{
		logte("Could not register transcoder application to mediarouter. [%s]", app_info.GetVHostAppName().CStr());

		_router->UnregisterObserverApp(app_info, application);
		ov::LockGuard lock(_transcode_apps_mutex);
		_transcode_apps.erase(application_id);
		return false;
	}

	logti("Transcoder has created [%s][%s] application", app_info.IsDynamicApp() ? "dynamic" : "config", app_info.GetVHostAppName().CStr());

	return true;
}

// Delete Application
bool Transcoder::OnDeleteApplication(const info::Application &app_info)
{
	auto application_id = app_info.GetId();

	std::shared_ptr<TranscodeApplication> application;
	{
		ov::LockGuard lock(_transcode_apps_mutex);
		auto it = _transcode_apps.find(application_id);
		if (it == _transcode_apps.end())
		{
			return false;
		}
		application = it->second;
		_transcode_apps.erase(it);
	}

	if (application == nullptr)
	{
		return true;
	}

	// Unregister to MediaRouter
	if (_router->UnregisterObserverApp(app_info, application) == false)
	{
		logte("Could not unregister the application: %p", application.get());
	}

	// Unregister to MediaRouter
	if (_router->UnregisterConnectorApp(app_info, application) == false)
	{
		logte("Could not unregister the application: %p", application.get());
	}

	logti("Transcoder has deleted [%s][%s] application", app_info.IsDynamicApp() ? "dynamic" : "config", app_info.GetVHostAppName().CStr());

	return true;
}

//  Application Name으로 TranscodeApplication 찾음
std::shared_ptr<TranscodeApplication> Transcoder::GetApplicationById(info::application_id_t application_id)
{
	ov::SharedLockGuard lock(_transcode_apps_mutex);
	auto obj = _transcode_apps.find(application_id);
	if (obj == _transcode_apps.end())
	{
		return nullptr;
	}

	return obj->second;
}

bool Transcoder::PauseEncoders(const info::VHostAppName &vhost_app_name, const ov::String &stream_name, cmn::MediaCodecId codec_id)
{
	ov::SharedLockGuard lock(_transcode_apps_mutex);
	for (auto &[id, app] : _transcode_apps)
	{
		if (app && app->GetApplicationInfo().GetVHostAppName() == vhost_app_name)
		{
			return app->PauseEncoders(stream_name, codec_id);
		}
	}
	return false;
}

bool Transcoder::ResumeEncoders(const info::VHostAppName &vhost_app_name, const ov::String &stream_name, cmn::MediaCodecId codec_id)
{
	ov::SharedLockGuard lock(_transcode_apps_mutex);
	for (auto &[id, app] : _transcode_apps)
	{
		if (app && app->GetApplicationInfo().GetVHostAppName() == vhost_app_name)
		{
			return app->ResumeEncoders(stream_name, codec_id);
		}
	}
	return false;
}

bool Transcoder::IsEncoderPaused(const info::VHostAppName &vhost_app_name, const ov::String &stream_name, cmn::MediaCodecId codec_id)
{
	ov::SharedLockGuard lock(_transcode_apps_mutex);
	for (auto &[id, app] : _transcode_apps)
	{
		if (app && app->GetApplicationInfo().GetVHostAppName() == vhost_app_name)
		{
			return app->IsEncoderPaused(stream_name, codec_id);
		}
	}
	return false;
}

std::vector<TranscodeEncoder::EncoderInfo> Transcoder::GetEncoderInfoList(const info::VHostAppName &vhost_app_name, const ov::String &stream_name, cmn::MediaCodecId codec_id)
{
	ov::SharedLockGuard lock(_transcode_apps_mutex);
	for (auto &[id, app] : _transcode_apps)
	{
		if (app && app->GetApplicationInfo().GetVHostAppName() == vhost_app_name)
		{
			return app->GetEncoderInfoList(stream_name, codec_id);
		}
	}
	return {};
}
