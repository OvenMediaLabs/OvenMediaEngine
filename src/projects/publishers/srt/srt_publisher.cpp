//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2024 AirenSoft. All rights reserved.
//
//==============================================================================
#include "srt_publisher.h"

#include <base/ovlibrary/url.h>
#include <modules/srt/srt.h>

#include "srt_private.h"
#include "srt_session.h"

namespace pub
{
	std::shared_ptr<SrtPublisher> SrtPublisher::Create(const cfg::Server &server_config, const std::shared_ptr<MediaRouterInterface> &router)
	{
		auto obj = std::make_shared<SrtPublisher>(server_config, router);

		return obj->Start() ? obj : nullptr;
	}

	SrtPublisher::SrtPublisher(const cfg::Server &server_config, const std::shared_ptr<MediaRouterInterface> &router)
		: Publisher(server_config, router)
	{
	}

	SrtPublisher::~SrtPublisher()
	{
		logtt("%s has finally been terminated", GetPublisherName());
	}

	bool SrtPublisher::Start()
	{
		const auto &server_config = GetServerConfig();
		const auto &srt_bind_config = server_config.GetBind().GetPublishers().GetSrt();

		if (srt_bind_config.IsParsed() == false)
		{
			logtw("%s is disabled by configuration", GetPublisherName());
			return true;
		}

		// Init StreamMap if exist
		_stream_url_resolver.Initialize(srt_bind_config.GetStreamMap().GetStreamList());

		bool is_configured;
		auto &port_config = srt_bind_config.GetPort(&is_configured);

		if (is_configured == false)
		{
			logtw("SRT Publisher is disabled - No port is configured");
			return true;
		}

		auto &ip_list = server_config.GetIPList();
		std::vector<ov::SocketAddress> address_list;
		try
		{
			address_list = ov::SocketAddress::Create(ip_list, port_config.GetPortList());
		}
		catch (const ov::Error &e)
		{
			logte("Could not listen for %s Server: %s", GetPublisherName(), e.What());
			return false;
		}

		bool result = true;
		std::vector<ov::String> address_string_list;
		std::vector<std::shared_ptr<PhysicalPort>> physical_port_list;

		auto physical_port_manager = PhysicalPortManager::GetInstance();

		auto worker_count = srt_bind_config.GetWorkerCount(&is_configured);
		worker_count = is_configured ? worker_count : PHYSICAL_PORT_USE_DEFAULT_COUNT;

		for (const auto &address : address_list)
		{
			auto physical_port = physical_port_manager->CreatePort(
				"SRTPub", ov::SocketType::Srt, address, worker_count, false, 0, 0,
				[=](const std::shared_ptr<ov::Socket> &socket) -> std::shared_ptr<ov::Error> {
					return SrtOptionProcessor::SetOptions(socket, srt_bind_config.GetOptions());
				});

			if (physical_port == nullptr)
			{
				logte("Could not initialize phyiscal port for %s on %s", GetPublisherName(), address.ToString().CStr());
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
				  GetPublisherName(),
				  ov::String::Join(address_string_list, ", ").CStr(),
				  ov::StringFromSocketType(ov::SocketType::Srt));

			{
				std::lock_guard lock_guard(_physical_port_list_mutex);
				_physical_port_list = std::move(physical_port_list);
			}

			_disconnect_timer.Push(
				[this](void *parameter) {
					if (_has_socket_list_to_disconnect)
					{
						decltype(_socket_list_to_disconnect) socket_list_to_disconnect;

						{
							std::lock_guard lock_guard(_socket_list_to_disconnect_mutex);
							_has_socket_list_to_disconnect = false;
							socket_list_to_disconnect = std::move(_socket_list_to_disconnect);
						}

						for (const auto &socket : socket_list_to_disconnect)
						{
							socket->Close();
						}
					}

					return ov::DelayQueueAction::Repeat;
				},
				100);
			_disconnect_timer.Start();

			return Publisher::Start();
		}

		for (auto &physical_port : physical_port_list)
		{
			physical_port->RemoveObserver(this);
			physical_port_manager->DeletePort(physical_port);
		}

		return false;
	}

	bool SrtPublisher::Stop()
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

		_disconnect_timer.Stop();
		_disconnect_timer.Clear();

		return Publisher::Stop();
	}

	bool SrtPublisher::OnCreateHost(const info::Host &host_info)
	{
		return true;
	}

	bool SrtPublisher::OnDeleteHost(const info::Host &host_info)
	{
		return true;
	}

	std::shared_ptr<Application> SrtPublisher::OnCreatePublisherApplication(const info::Application &application_info)
	{
		if (IsModuleAvailable() == false)
		{
			return nullptr;
		}

		return SrtApplication::Create(SrtPublisher::GetSharedPtrAs<Publisher>(), application_info);
	}

	bool SrtPublisher::OnDeletePublisherApplication(const std::shared_ptr<Application> &application)
	{
		return true;
	}

	void SrtPublisher::AddToDisconnect(const std::shared_ptr<ov::Socket> &remote)
	{
		std::lock_guard lock(_socket_list_to_disconnect_mutex);
		_has_socket_list_to_disconnect = true;
		_socket_list_to_disconnect.emplace_back(remote);
	}

	void SrtPublisher::OnConnected(const std::shared_ptr<ov::Socket> &remote)
	{
		auto orchestrator = ocst::Orchestrator::GetInstance();

		logti("The SRT client has connected: %s", remote->ToString().CStr());

		auto [final_url, host_info_item] = _stream_url_resolver.Resolve(remote);

		if ((final_url == nullptr) || (host_info_item.has_value() == false))
		{
			AddToDisconnect(remote);
			return;
		}

		const auto &host_info = host_info_item.value();

		auto requested_url = final_url;

		auto remote_address = remote->GetRemoteAddress();
		auto request_info = std::make_shared<ac::RequestInfo>(final_url, nullptr, remote, nullptr);

		//
		// Handle SignedPolicy
		//
		uint64_t life_time = 0;
		auto [signed_policy_result, signed_policy] = VerifyBySignedPolicy(host_info, request_info);

		switch (signed_policy_result)
		{
			case AccessController::VerificationResult::Error:
				// will not reach here
				AddToDisconnect(remote);
				return;

			case AccessController::VerificationResult::Fail:
				logtw("%s", signed_policy->GetErrMessage().CStr());
				AddToDisconnect(remote);
				return;

			case AccessController::VerificationResult::Off:
				// Success
				break;

			case AccessController::VerificationResult::Pass:
				life_time = signed_policy->GetStreamExpireEpochMSec();
				break;
		}

		//
		// Handle AdmissionWebhooks
		//
		auto [webhooks_result, admission_webhooks] = VerifyByAdmissionWebhooks(host_info, request_info);

		auto vhost_app_name = orchestrator->ResolveApplicationName(host_info.GetName(), final_url->App());
		auto stream_name = final_url->Stream();

		switch (webhooks_result)
		{
			case AccessController::VerificationResult::Error:
				logte("An error occurred while verifying with the AdmissionWebhooks: %s", final_url->ToUrlString().CStr());
				AddToDisconnect(remote);
				return;

			case AccessController::VerificationResult::Fail:
				logtw("AdmissionWebhooks server returns an error: %s", admission_webhooks->GetErrReason().CStr());
				AddToDisconnect(remote);
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
					if ((life_time == 0) || (stream_expired_msec_from_webhooks < life_time))
					{
						life_time = stream_expired_msec_from_webhooks;
					}
				}

				// Redirect URL
				if (admission_webhooks->GetNewURL() != nullptr)
				{
					final_url = admission_webhooks->GetNewURL();
					vhost_app_name = orchestrator->ResolveApplicationNameFromDomain(final_url->Host(), final_url->App());
					stream_name = final_url->Stream();
				}
				break;
		}

		// Check if application is exist
		auto application = GetApplicationByName(vhost_app_name);
		if (application == nullptr)
		{
			logte("Could not find vhost/app: %s", vhost_app_name.CStr());
			AddToDisconnect(remote);
			return;
		}

		auto stream = application->GetStreamAs<SrtStream>(final_url->Stream());

		if (stream == nullptr)
		{
			stream = std::dynamic_pointer_cast<SrtStream>(PullStream(final_url, vhost_app_name, final_url->Host(), final_url->Stream()));
		}

		if(stream == nullptr)
		{
			logte("Could not find stream: %s", final_url->Stream().CStr());
			AddToDisconnect(remote);
			return;
		}

		if (stream->GetState() != Stream::State::STARTED)
		{
			logtw("The stream is not started: %s/%u", stream->GetName().CStr(), stream->GetId());
			AddToDisconnect(remote);
			return;
		}

		auto playlist_name = final_url->File();
		std::shared_ptr<SrtPlaylist> srt_playlist = nullptr;

		if (playlist_name.IsEmpty())
		{
			// Use default playlist
			playlist_name = stream->GetDefaultPlaylistInfo()->file_name;
		}

		srt_playlist = stream->GetSrtPlaylist(playlist_name);

		if (srt_playlist == nullptr)
		{
			logte("Could not find playlist: %s", final_url->File().CStr());
			remote->Close();
			return;
		}

		auto session = SrtSession::Create(application, stream, remote->GetNativeHandle(), remote, srt_playlist);

		{
			std::unique_lock lock(_session_map_mutex);
			_session_map.emplace(session->GetId(), session);
		}

		session->SetRequestedUrl(requested_url);
		session->SetFinalUrl(final_url);

		stream->AddSession(session);
	}

	void SrtPublisher::OnDataReceived(const std::shared_ptr<ov::Socket> &remote,
									  const ov::SocketAddress &address,
									  const std::shared_ptr<const ov::Data> &data)
	{
		// Nothing to do
	}

	std::shared_ptr<SrtSession> SrtPublisher::GetSession(const std::shared_ptr<ov::Socket> &remote)
	{
		const auto session_id = remote->GetNativeHandle();

		std::shared_lock lock(_session_map_mutex);
		auto item = _session_map.find(session_id);

		return (item != _session_map.end()) ? item->second : nullptr;
	}

	void SrtPublisher::OnDisconnected(const std::shared_ptr<ov::Socket> &remote,
									  PhysicalPortDisconnectReason reason,
									  const std::shared_ptr<const ov::Error> &error)
	{
		auto session = GetSession(remote);

		if (session == nullptr)
		{
			logtt("Failed to find channel to delete SRT stream (remote : %s)", remote->ToString().CStr());
			return;
		}

		{
			std::unique_lock lock(_session_map_mutex);
			_session_map.erase(remote->GetNativeHandle());
		}

		auto stream = session->GetStream();
		stream->RemoveSession(session->GetId());

		// Send Close to Admission Webhooks
		auto requested_url = session->GetRequestedUrl();
		auto final_url = session->GetFinalUrl();

		if ((requested_url != nullptr) && (final_url != nullptr))
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

				// Checking the return value is not necessary
				SendCloseAdmissionWebhooks(host_info_item.value(), request_info);
			}
		}

		logti("The SRT client has disconnected: [%s/%s], %s", stream->GetApplicationName(), stream->GetName().CStr(), remote->ToString().CStr());
	}
}  // namespace pub
