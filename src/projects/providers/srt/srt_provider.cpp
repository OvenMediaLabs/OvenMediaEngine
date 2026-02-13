//==============================================================================
//
//  SRT Provider
//
//  Created by Getroot
//  Copyright (c) 2021 AirenSoft. All rights reserved.
//
//==============================================================================

#include "srt_provider.h"

#include <modules/physical_port/physical_port_manager.h>
#include <modules/srt/srt.h>
#include <orchestrator/orchestrator.h>

#include "providers/mpegts/mpegts_application.h"
#include "providers/mpegts/mpegts_stream.h"
#include "srt_provider_private.h"

namespace pvd
{
	std::shared_ptr<SrtProvider> SrtProvider::Create(const cfg::Server &server_config, const std::shared_ptr<MediaRouterInterface> &router)
	{
		auto provider = std::make_shared<SrtProvider>(server_config, router);
		if (!provider->Start())
		{
			return nullptr;
		}
		return provider;
	}

	SrtProvider::SrtProvider(const cfg::Server &server_config, const std::shared_ptr<MediaRouterInterface> &router)
		: PushProvider(server_config, router)
	{
		logtt("Created SRT Provider module.");
	}

	SrtProvider::~SrtProvider()
	{
		logti("Terminated SRT Provider module.");
	}

	bool SrtProvider::Start()
	{
		auto &server_config = GetServerConfig();
		auto &srt_bind_config = server_config.GetBind().GetProviders().GetSrt();

		if (srt_bind_config.IsParsed() == false)
		{
			logtw("%s is disabled by configuration", GetProviderName());
			return true;
		}

		// Init StreamMap if exist
		_stream_url_resolver.Initialize(srt_bind_config.GetStreamMap().GetStreamList());

		bool is_configured;
		bool is_port_configured;
		auto &port_config = srt_bind_config.GetPort(&is_port_configured);

		if (is_port_configured == false)
		{
			logtw("%s is disabled - No port is configured", GetProviderName());
			return true;
		}

		std::vector<ov::SocketAddress> address_list;
		try
		{
			address_list = ov::SocketAddress::Create(server_config.GetIPList(), port_config.GetPortList());
		}
		catch (const ov::Error &e)
		{
			logte("Could not listen for %s Server: %s", GetProviderName(), e.What());
			return false;
		}

		bool result = true;
		std::vector<ov::String> address_string_list;
		std::vector<std::shared_ptr<PhysicalPort>> physical_port_list;

		auto physical_port_manager = PhysicalPortManager::GetInstance();

		auto worker_count = srt_bind_config.GetWorkerCount(&is_configured);
		worker_count = is_configured ? worker_count : PHYSICAL_PORT_USE_DEFAULT_COUNT;
		auto thread_per_socket = srt_bind_config.IsThreadPerSocket();

		for (const auto &address : address_list)
		{
			auto physical_port = physical_port_manager->CreatePort(
				"SRT", ov::SocketType::Srt, address, worker_count, thread_per_socket, 0, 0,
				[=](const std::shared_ptr<ov::Socket> &socket) -> std::shared_ptr<ov::Error> {
					return SrtOptionProcessor::SetOptions(socket, srt_bind_config.GetOptions());
				});

			if (physical_port == nullptr)
			{
				logte("Could not initialize phyiscal port for %s on %s", GetProviderName(), address.ToString().CStr());
				result = false;
				break;
			}

			address_string_list.emplace_back(address.ToString());

			physical_port->AddObserver(this);
			physical_port_list.push_back(physical_port);
		}

		if (result)
		{
			logti("%s is listening on %s/%s",
				  GetProviderName(),
				  ov::String::Join(address_string_list, ", ").CStr(),
				  ov::StringFromSocketType(ov::SocketType::Srt));

			{
				std::lock_guard lock_guard{_physical_port_list_mutex};
				_physical_port_list = std::move(physical_port_list);
			}

			return PushProvider::Start();
		}

		for (auto &physical_port : physical_port_list)
		{
			physical_port->RemoveObserver(this);
			physical_port_manager->DeletePort(physical_port);
		}

		return false;
	}

	bool SrtProvider::Stop()
	{
		_physical_port_list_mutex.lock();
		auto physical_port_list = std::move(_physical_port_list);
		_physical_port_list_mutex.unlock();

		auto physical_port_manager = PhysicalPortManager::GetInstance();

		for (auto &physical_port : physical_port_list)
		{
			physical_port->RemoveObserver(this);
			physical_port_manager->DeletePort(physical_port);
		}

		return PushProvider::Stop();
	}

	bool SrtProvider::OnCreateHost(const info::Host &host_info)
	{
		return true;
	}

	bool SrtProvider::OnDeleteHost(const info::Host &host_info)
	{
		return true;
	}

	std::shared_ptr<pvd::Application> SrtProvider::OnCreateProviderApplication(const info::Application &application_info)
	{
		if (IsModuleAvailable() == false)
		{
			return nullptr;
		}

		auto application = MpegTsApplication::Create(GetSharedPtrAs<pvd::PushProvider>(), application_info);
		if (application == nullptr)
		{
			return nullptr;
		}

		auto audio_map = application_info.GetConfig().GetProviders().GetSrtProvider().GetAudioMap();
		application->AddAudioMapItems(audio_map);

		return application;
	}

	bool SrtProvider::OnDeleteProviderApplication(const std::shared_ptr<pvd::Application> &application)
	{
		return PushProvider::OnDeleteProviderApplication(application);
	}

	void SrtProvider::OnConnected(const std::shared_ptr<ov::Socket> &remote)
	{
		auto orchestrator = ocst::Orchestrator::GetInstance();
		
		logti("The SRT client has connected : %s [%d] [%s]", remote->ToString().CStr(), remote->GetNativeHandle(), remote->GetStreamId().CStr());

		auto [final_url, host_info_item] = _stream_url_resolver.Resolve(remote);

		if ((final_url == nullptr) || (host_info_item.has_value() == false))
		{
			remote->Close();
			return;
		}

		const auto &host_info = host_info_item.value();

		auto requested_url = final_url;

		auto channel_id = remote->GetNativeHandle();

		//TODO(Getroot): For security enhancement,
		// it should be checked whether the actual ip:port is the same as the ip:port of streamid (after dns resolve if it is domain).

		auto remote_address = remote->GetRemoteAddress();
		auto request_info = std::make_shared<ac::RequestInfo>(final_url, nullptr, remote, nullptr);

		// SignedPolicy
		uint64_t life_time = 0;
		
		auto [signed_policy_result, signed_policy] = VerifyBySignedPolicy(host_info, request_info);

		switch (signed_policy_result)
		{
			case AccessController::VerificationResult::Error:
				// will not reach here
				remote->Close();
				return;

			case AccessController::VerificationResult::Fail:
				logtw("%s", signed_policy->GetErrMessage().CStr());
				remote->Close();
				return;

			case AccessController::VerificationResult::Off:
				// Success
				break;

			case AccessController::VerificationResult::Pass:
				life_time = signed_policy->GetStreamExpireEpochMSec();
				break;
		}

		// Admission Webhooks
		auto [webhooks_result, admission_webhooks] = VerifyByAdmissionWebhooks(host_info, request_info);

		auto vhost_app_name						   = orchestrator->ResolveApplicationName(host_info.GetName(), final_url->App());
		auto stream_name						   = final_url->Stream();

		switch (webhooks_result)
		{
			case AccessController::VerificationResult::Error:
				logtw("AdmissionWebhooks error : %s", final_url->ToUrlString().CStr());
				remote->Close();
				return;

			case AccessController::VerificationResult::Fail:
				logtw("AdmissionWebhooks error : %s", admission_webhooks->GetErrReason().CStr());
				remote->Close();
				return;

			case AccessController::VerificationResult::Off:
				// Success
				break;

			case AccessController::VerificationResult::Pass:
				// Lifetime
				if (admission_webhooks->GetLifetime() != 0)
				{
					// Choice smaller value
					auto stream_expired_msec_from_webhooks = ov::Clock::NowMSec() + admission_webhooks->GetLifetime();
					if (life_time == 0 || stream_expired_msec_from_webhooks < life_time)
					{
						life_time = stream_expired_msec_from_webhooks;
					}
				}

				// Redirect URL
				if (admission_webhooks->GetNewURL() != nullptr)
				{
					final_url	   = admission_webhooks->GetNewURL();
					vhost_app_name = orchestrator->ResolveApplicationNameFromDomain(final_url->Host(), final_url->App());
					stream_name	   = final_url->Stream();
				}
				break;
		}

		// Check if application is exist
		if (GetApplicationByName(vhost_app_name) == nullptr)
		{
			logte("Could not find vhost/app: %s", vhost_app_name.CStr());
			remote->Close();
			return;
		}

		auto stream = MpegTsStream::Create(StreamSourceType::Srt, channel_id, vhost_app_name, stream_name, remote, *remote->GetRemoteAddress(), life_time, GetSharedPtrAs<pvd::PushProvider>());
		stream->SetRequestedUrl(requested_url);
		stream->SetFinalUrl(final_url);

		PushProvider::OnChannelCreated(remote->GetNativeHandle(), stream);
	}

	void SrtProvider::OnDataReceived(const std::shared_ptr<ov::Socket> &remote,
									 const ov::SocketAddress &address,
									 const std::shared_ptr<const ov::Data> &data)
	{
		auto channel_id = remote->GetNativeHandle();
		PushProvider::OnDataReceived(channel_id, data);
	}

	void SrtProvider::OnTimedOut(const std::shared_ptr<PushStream> &channel)
	{
		
	}

	void SrtProvider::OnDisconnected(const std::shared_ptr<ov::Socket> &remote,
									 PhysicalPortDisconnectReason reason,
									 const std::shared_ptr<const ov::Error> &error)
	{
		logti("SrtProvider::OnDisonnected : %s [%s]", remote->ToString().CStr(), remote->GetStreamId().CStr());

		auto channel = GetChannel(remote->GetNativeHandle());
		if (channel == nullptr)
		{
			// It probably rejected on OnConnected
			logtt("Failed to find channel to delete stream (remote : %s)", remote->ToString().CStr());
			return;
		}

		// Send Close to Admission Webhooks
		auto requested_url = channel->GetRequestedUrl();
		auto final_url = channel->GetFinalUrl();
		if (remote && requested_url && final_url)
		{
			auto [new_final_url, host_info_item] = _stream_url_resolver.Resolve(remote);

			if ((new_final_url == nullptr) || (host_info_item.has_value() == false))
			{
				OV_ASSERT2(false);
				logte("Failed to resolve final URL for SRT disconnection: %s", remote->ToString().CStr());
			}
			else
			{
				auto request_info = std::make_shared<ac::RequestInfo>(requested_url, final_url, remote, nullptr);

				SendCloseAdmissionWebhooks(host_info_item.value(), request_info);
			}
		}
		// the return check is not necessary

		logti("The SRT client has disconnected: [%s/%s], remote: %s", channel->GetApplicationName(), channel->GetName().CStr(), remote->ToString().CStr());

		PushProvider::OnChannelDeleted(remote->GetNativeHandle());
	}
}  // namespace pvd
