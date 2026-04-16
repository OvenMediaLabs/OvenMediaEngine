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
		for (const auto &entry : whisper_cfg.GetPreloadModels())
		{
			ov::String resolved = ov::GetFilePath(entry.GetPath(), config_path);

			// Parse <Devices>:
			// - Omitted/empty → device 0 (default)
			// - "all" → empty list passed to Preload (= load on every available GPU)
			// - "0,1" etc → specific device indices
			std::vector<int32_t> device_ids;
			const ov::String &devices_str = entry.GetDevices();
			if (devices_str.IsEmpty())
			{
				device_ids.push_back(0);
			}
			else if (devices_str.LowerCaseString() != "all")
			{
				for (const auto &token : devices_str.Split(","))
				{
					ov::String trimmed = token.Trim();
					if (!trimmed.IsEmpty())
					{
						device_ids.push_back(ov::Converter::ToInt32(trimmed));
					}
				}
			}
			// "all" → device_ids remains empty → Preload loads on all available GPUs

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
		std::unique_lock<std::shared_mutex> lock(_transcode_apps_mutex);
		_transcode_apps[application_id] = application;
	}

	// Register to MediaRouter
	if (_router->RegisterObserverApp(app_info, application) == false)
	{
		logte("Could not register transcoder application to mediarouter. [%s]", app_info.GetVHostAppName().CStr());

		std::unique_lock<std::shared_mutex> lock(_transcode_apps_mutex);
		_transcode_apps.erase(application_id);
		return false;
	}

	// Register to MediaRouter
	if (_router->RegisterConnectorApp(app_info, application) == false)
	{
		logte("Could not register transcoder application to mediarouter. [%s]", app_info.GetVHostAppName().CStr());

		_router->UnregisterObserverApp(app_info, application);
		std::unique_lock<std::shared_mutex> lock(_transcode_apps_mutex);
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
		std::unique_lock<std::shared_mutex> lock(_transcode_apps_mutex);
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
	std::shared_lock<std::shared_mutex> lock(_transcode_apps_mutex);
	auto obj = _transcode_apps.find(application_id);
	if (obj == _transcode_apps.end())
	{
		return nullptr;
	}

	return obj->second;
}

bool Transcoder::PauseEncoders(const info::VHostAppName &vhost_app_name, const ov::String &stream_name, cmn::MediaCodecId codec_id)
{
	std::shared_lock<std::shared_mutex> lock(_transcode_apps_mutex);
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
	std::shared_lock<std::shared_mutex> lock(_transcode_apps_mutex);
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
	std::shared_lock<std::shared_mutex> lock(_transcode_apps_mutex);
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
	std::shared_lock<std::shared_mutex> lock(_transcode_apps_mutex);
	for (auto &[id, app] : _transcode_apps)
	{
		if (app && app->GetApplicationInfo().GetVHostAppName() == vhost_app_name)
		{
			return app->GetEncoderInfoList(stream_name, codec_id);
		}
	}
	return {};
}
