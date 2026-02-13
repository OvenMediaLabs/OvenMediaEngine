//==============================================================================
//
//  RtmpProvider
//
//  Created by Getroot
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================
#include "rtmp_provider.h"

#include <base/info/media_extradata.h>
#include <config/config.h>
#include <modules/physical_port/physical_port_manager.h>
#include <unistd.h>

#include <iostream>

#include "rtmp_application.h"
#include "rtmp_provider_private.h"
#include "rtmp_stream.h"
#include "rtmp_stream_v2.h"

namespace pvd
{
#if DEBUG
	static bool dump_packet = false;
	static void DumpDataToFile(const std::shared_ptr<ov::Socket> &remote,
							   const ov::SocketAddress &address,
							   const std::shared_ptr<const ov::Data> &data)
	{
		if (dump_packet)
		{
			auto remote_address = remote->GetRemoteAddress();

			if (remote_address != nullptr)
			{
				auto file_name = remote_address->ToString().Replace(":", "_");

				ov::DumpToFile(ov::PathManager::Combine(ov::PathManager::GetAppPath("dump/rtmp"), file_name), data, 0L, true);
			}
		}
	}
#endif	// DEBUG

	std::shared_ptr<RtmpProvider> RtmpProvider::Create(const cfg::Server &server_config, const std::shared_ptr<MediaRouterInterface> &router)
	{
#if DEBUG
		dump_packet = ov::Converter::ToBool(std::getenv("OME_DUMP_RTMP"));

		if (dump_packet)
		{
			logtw("RTMP packet dump is enabled. Dump files are saved in %s", ov::PathManager::GetAppPath("dump/rtmp").CStr());
		}
#endif	// DEBUG

		auto provider = std::make_shared<RtmpProvider>(server_config, router);
		if (!provider->Start())
		{
			return nullptr;
		}
		return provider;
	}

	RtmpProvider::RtmpProvider(const cfg::Server &server_config, const std::shared_ptr<MediaRouterInterface> &router)
		: PushProvider(server_config, router)
	{
		logtt("Created Rtmp Provider module.");
	}

	RtmpProvider::~RtmpProvider()
	{
		logti("Terminated Rtmp Provider module.");
	}

	bool RtmpProvider::Start()
	{
		if (_physical_port_list.empty() == false)
		{
			logtw("RTMP server is already running");
			return false;
		}

		auto server = GetServerConfig();
		const auto &rtmp_config = server.GetBind().GetProviders().GetRtmp();

		if (rtmp_config.IsParsed() == false)
		{
			logtw("%s is disabled by configuration", GetProviderName());
			return true;
		}

		bool is_configured;
		auto worker_count = rtmp_config.GetWorkerCount(&is_configured);
		worker_count = is_configured ? worker_count : PHYSICAL_PORT_USE_DEFAULT_COUNT;
		auto thread_per_socket = rtmp_config.IsThreadPerSocket();

		std::vector<ov::SocketAddress> rtmp_address_list;

		try
		{
			rtmp_address_list = ov::SocketAddress::Create(server.GetIPList(), static_cast<uint16_t>(rtmp_config.GetPort().GetPort()));
		}
		catch (const ov::Error &e)
		{
			logte("Could not listen for RTMP: %s", e.What());
			return false;
		}

		if (rtmp_address_list.empty())
		{
			logte("Could not obtain IP list from IP(s): %s, port: %d",
				  ov::String::Join(server.GetIPList(), ", ").CStr(),
				  static_cast<uint16_t>(rtmp_config.GetPort().GetPort()));

			return false;
		}

		auto port_manager = PhysicalPortManager::GetInstance();
		std::vector<ov::String> rtmp_address_string_list;

		for (const auto &rtmp_address : rtmp_address_list)
		{
			auto physical_port = port_manager->CreatePort("RTMP", ov::SocketType::Tcp, rtmp_address, worker_count, thread_per_socket);

			if (physical_port == nullptr)
			{
				logte("Could not initialize physical port for RTMP server: %s", rtmp_address.ToString().CStr());

				for (auto &physical_port : _physical_port_list)
				{
					physical_port->RemoveObserver(this);
					port_manager->DeletePort(physical_port);
				}
				_physical_port_list.clear();

				return false;
			}

			rtmp_address_string_list.emplace_back(rtmp_address.ToString());

			physical_port->AddObserver(this);
			_physical_port_list.push_back(physical_port);
		}

		_is_ertmp_enabled = server.GetModules().GetERTMP().IsEnabled();

		if (_is_ertmp_enabled)
		{
			logtw("E-RTMP is enabled, and this is an experimental feature");
		}

		logti("%s is listening on %s",
			  GetProviderName(),
			  ov::String::Join(rtmp_address_string_list, ", ").CStr());

		return PushProvider::Start();
	}

	bool RtmpProvider::Stop()
	{
		auto port_manager = PhysicalPortManager::GetInstance();

		for (auto &physical_port : _physical_port_list)
		{
			physical_port->RemoveObserver(this);
			port_manager->DeletePort(physical_port);
		}
		_physical_port_list.clear();

		return PushProvider::Stop();
	}

	bool RtmpProvider::OnCreateHost(const info::Host &host_info)
	{
		return true;
	}

	bool RtmpProvider::OnDeleteHost(const info::Host &host_info)
	{
		return true;
	}

	std::shared_ptr<pvd::Application> RtmpProvider::OnCreateProviderApplication(const info::Application &application_info)
	{
		if (IsModuleAvailable() == false)
		{
			return nullptr;
		}

		return RtmpApplication::Create(GetSharedPtrAs<pvd::PushProvider>(), application_info);
	}

	bool RtmpProvider::OnDeleteProviderApplication(const std::shared_ptr<pvd::Application> &application)
	{
		return PushProvider::OnDeleteProviderApplication(application);
	}

	template <typename T>
	std::shared_ptr<pvd::PushStream> CreateStream(int channel_id, const std::shared_ptr<ov::Socket> &remote, const std::shared_ptr<pvd::PushProvider> &provider)
	{
		return T::Create(StreamSourceType::Rtmp, channel_id, remote, provider);
	}

	void RtmpProvider::OnConnected(const std::shared_ptr<ov::Socket> &remote)
	{
		auto channel_id = remote->GetNativeHandle();
		auto stream = _is_ertmp_enabled
						  ? CreateStream<rtmp::RtmpStreamV2>(channel_id, remote, GetSharedPtrAs<pvd::PushProvider>())
						  : CreateStream<RtmpStream>(channel_id, remote, GetSharedPtrAs<pvd::PushProvider>());

		logti("A RTMP client has connected from %s%s", remote->ToString().CStr(),
			  _is_ertmp_enabled ? " (E-RTMP)" : "");

		PushProvider::OnChannelCreated(channel_id, stream);
	}

	void RtmpProvider::OnDataReceived(const std::shared_ptr<ov::Socket> &remote,
									  const ov::SocketAddress &address,
									  const std::shared_ptr<const ov::Data> &data)
	{
#if DEBUG
		DumpDataToFile(remote, address, data);
#endif	// DEBUG

		PushProvider::OnDataReceived(remote->GetNativeHandle(), data);
	}

	void RtmpProvider::OnDisconnected(const std::shared_ptr<ov::Socket> &remote,
									  PhysicalPortDisconnectReason reason,
									  const std::shared_ptr<const ov::Error> &error)
	{
		auto channel = GetChannel(remote->GetNativeHandle());
		if (channel == nullptr)
		{
			logte("Failed to find channel to delete stream (remote : %s)", remote->ToString().CStr());
			return;
		}

		logti("The RTMP client has disconnected: [%s], remote: %s",
			  channel->GetNamePath().CStr(),
			  remote->ToString().CStr());

		PushProvider::OnChannelDeleted(remote->GetNativeHandle());
	}
}  // namespace pvd