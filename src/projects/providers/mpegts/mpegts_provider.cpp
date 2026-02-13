//==============================================================================
//
//  MPEGTS Provider
//
//  Created by Getroot
//  Copyright (c) 2020 AirenSoft. All rights reserved.
//
//==============================================================================

#include "mpegts_provider.h"

#include <modules/physical_port/physical_port_manager.h>
#include <orchestrator/orchestrator.h>

#include "mpegts_application.h"
#include "mpegts_provider_private.h"
#include "mpegts_stream.h"

namespace pvd
{
	std::shared_ptr<MpegTsProvider> MpegTsProvider::Create(const cfg::Server &server_config, const std::shared_ptr<MediaRouterInterface> &router)
	{
		auto provider = std::make_shared<MpegTsProvider>(server_config, router);
		if (!provider->Start())
		{
			return nullptr;
		}
		return provider;
	}

	MpegTsProvider::MpegTsProvider(const cfg::Server &server_config, const std::shared_ptr<MediaRouterInterface> &router)
		: PushProvider(server_config, router)
	{
		logtt("Created Mpegts Provider module.");
	}

	MpegTsProvider::~MpegTsProvider()
	{
		logti("Terminated Mpegts Provider module.");
	}

	bool MpegTsProvider::BindMpegTSPorts()
	{
		auto &server_config = GetServerConfig();
		auto &ip_list = server_config.GetIPList();
		auto &mpegts_provider_config = server_config.GetBind().GetProviders().GetMpegts();
		auto &port_config = mpegts_provider_config.GetPort();
		auto &port_list_config = port_config.GetPortList();
		auto socket_type = port_config.GetSocketType();

		std::map<uint16_t, std::shared_ptr<MpegTsStreamPortItem>> stream_port_map;
		std::vector<ov::String> address_string_list;

		auto physical_port_manager = PhysicalPortManager::GetInstance();

		bool result = true;

		for (const auto &port : port_list_config)
		{
			std::vector<ov::SocketAddress> address_list;

			try
			{
				address_list = ov::SocketAddress::Create(ip_list, port);
			}
			catch (const ov::Error &e)
			{
				logte("Could not create socket address: %s", e.What());
				return false;
			}

			std::vector<std::shared_ptr<PhysicalPort>> physical_port_list;

			for (const auto &address : address_list)
			{
				auto physical_port = physical_port_manager->CreatePort("MPEGTS", socket_type, address, 1);

				if (physical_port == nullptr)
				{
					logte("Could not initialize phyiscal port for MPEG-TS server: %s/%s", address.ToString().CStr(), ov::StringFromSocketType(socket_type));
					result = false;
					break;
				}
				else
				{
					address_string_list.emplace_back(address.ToString());
				}

				physical_port->AddObserver(this);

				physical_port_list.emplace_back(physical_port);
			}

			stream_port_map.emplace(port, std::make_shared<MpegTsStreamPortItem>(socket_type, port, physical_port_list));
		}

		if (result)
		{
			auto socket_type_string = ov::StringFromSocketType(socket_type);
			ov::String suffix = ov::String::FormatString("/%s, ", socket_type_string);
			logti("%s is listening on %s/%s", GetProviderName(), ov::String::Join(address_string_list, suffix).CStr(), socket_type_string);

			{
				std::unique_lock lock_guard{_stream_port_map_lock};
				_stream_port_map = std::move(stream_port_map);
			}
			return true;
		}

		for (const auto &stream_port : stream_port_map)
		{
			auto physical_port_list = stream_port.second->GetPhysicalPortList();

			for (auto &physical_port : physical_port_list)
			{
				physical_port->RemoveObserver(this);
				physical_port_manager->DeletePort(physical_port);
			}
		}

		return false;
	}

	std::shared_ptr<MpegTsStreamPortItem> MpegTsProvider::GetDetachedStreamPortItem()
	{
		std::shared_lock<std::shared_mutex> lock(_stream_port_map_lock);

		for (const auto &item : _stream_port_map)
		{
			auto &stream_port_item = item.second;

			if (stream_port_item->IsAttached() == false)
			{
				return stream_port_item;
			}
		}

		return nullptr;
	}

	bool MpegTsProvider::Start()
	{
		auto &server_config = GetServerConfig();

		if (server_config.GetBind().GetProviders().GetMpegts().IsParsed() == false)
		{
			logtw("%s is disabled by configuration", GetProviderName());
			return true;
		}

		if (BindMpegTSPorts() == false)
		{
			return false;
		}

		return PushProvider::Start();
	}

	bool MpegTsProvider::Stop()
	{
		auto stream_port_map = std::move(_stream_port_map);

		for (const auto &x : stream_port_map)
		{
			auto stream_port_item = x.second;
			auto physical_port_list = stream_port_item->GetPhysicalPortList();

			for (auto &physical_port : physical_port_list)
			{
				physical_port->RemoveObserver(this);
				PhysicalPortManager::GetInstance()->DeletePort(physical_port);
			}
		}

		return PushProvider::Stop();
	}

	bool MpegTsProvider::OnCreateHost(const info::Host &host_info)
	{
		return true;
	}

	bool MpegTsProvider::OnDeleteHost(const info::Host &host_info)
	{
		return true;
	}

	std::shared_ptr<Application> MpegTsProvider::OnCreateProviderApplication(const info::Application &application_info)
	{
		if (IsModuleAvailable() == false)
		{
			return nullptr;
		}

		auto &app_config = application_info.GetConfig();
		auto &stream_list = app_config.GetProviders().GetMpegtsProvider().GetStreamMap().GetStreamList();
		auto app_metrics = ApplicationMetrics(application_info);

		for (auto &stream_item : stream_list)
		{
			bool is_parsed;
			auto &port_config = stream_item.GetPort(&is_parsed);
			std::vector<int> port_list;

			// If they want to use any available port
			if (is_parsed == false)
			{
				// Get random port
				auto stream_port_item = GetDetachedStreamPortItem();
				if (stream_port_item == nullptr)
				{
					logte("The %s application could not be created in %s provider because there are no ports available.",
						  application_info.GetVHostAppName().CStr(), GetProviderName());
					return nullptr;
				}

				port_list.push_back(stream_port_item->GetPortNumber());
			}
			else
			{
				port_list = port_config.GetPortList();
			}

			for (auto port : port_list)
			{
				// Search for the port in bound mpegts ports map
				auto stream_port_item = GetStreamPortItem(port);
				if (stream_port_item == nullptr || stream_port_item->IsAttached() == true)
				{
					logte("The %s application could not be created in %s provider because port %d requested to be assigned to mpegts is not available.",
						  application_info.GetVHostAppName().CStr(), GetProviderName(), port);
					return nullptr;
				}

				auto stream_name = stream_item.GetName().Replace("${Port}", ov::Converter::ToString(port));
				stream_port_item->AttachToApplication(application_info.GetVHostAppName(), stream_name);

				auto url = ov::Url::Parse(ov::String::FormatString("%s://0.0.0.0:%d", ov::StringFromSocketType(stream_port_item->GetScheme()), stream_port_item->GetPortNumber()));
				app_metrics->OnStreamReserved(GetProviderType(), *url, stream_name);
			}
		}

		auto application = MpegTsApplication::Create(GetSharedPtrAs<PushProvider>(), application_info);
		if (application == nullptr)
		{
			return nullptr;
		}

		auto audio_map = app_config.GetProviders().GetMpegtsProvider().GetAudioMap();
		application->AddAudioMapItems(audio_map);

		return application;
	}

	bool MpegTsProvider::OnDeleteProviderApplication(const std::shared_ptr<Application> &application)
	{
		std::shared_lock<std::shared_mutex> lock(_stream_port_map_lock);

		for (const auto &item : _stream_port_map)
		{
			auto &stream_port_item = item.second;
			if (stream_port_item->GetVhostAppName() == application->GetVHostAppName())
			{
				stream_port_item->DetachFromApplication();
			}
		}

		return PushProvider::OnDeleteProviderApplication(application);
	}

	std::shared_ptr<MpegTsStreamPortItem> MpegTsProvider::GetStreamPortItem(uint16_t local_port)
	{
		std::shared_lock<std::shared_mutex> lock(_stream_port_map_lock);

		auto x = _stream_port_map.find(local_port);
		if (x == _stream_port_map.end())
		{
			// something wrong
			return nullptr;
		}
		return x->second;
	}

	// This function is not called by PhysicalPort when the protocol is udp (MPEGTS/UDP)
	void MpegTsProvider::OnConnected(const std::shared_ptr<ov::Socket> &remote)
	{
		if (remote->GetRemoteAddress() == nullptr)
		{
			logte("A client connecting via MPEG-TS/TCP must have the remote address information");
			return;
		}

		OnConnected(remote, *remote->GetRemoteAddress());
	}

	bool MpegTsProvider::OnConnected(const std::shared_ptr<ov::Socket> &remote, const ov::SocketAddress &remote_address)
	{
		auto local_port = remote->GetLocalAddress()->Port();
		auto channel_id = remote->GetNativeHandle();

		auto stream_port_item = GetStreamPortItem(local_port);
		if (stream_port_item == nullptr || stream_port_item->IsAttached() == false)
		{
			return false;
		}

		auto stream = MpegTsStream::Create(StreamSourceType::Mpegts, channel_id, stream_port_item->GetVhostAppName(), stream_port_item->GetOutputStreamName(), remote, remote_address, 0, GetSharedPtrAs<PushProvider>());
		if (PushProvider::OnChannelCreated(remote->GetNativeHandle(), stream) == true)
		{
			logti("A MPEG-TS client has connected");  // %s", remote->ToString().CStr());
			stream_port_item->OnClientConnected(channel_id);
		}

		return true;
	}

	void MpegTsProvider::OnDatagramReceived(const std::shared_ptr<ov::Socket> &remote,
											const ov::SocketAddressPair &address_pair,
											const std::shared_ptr<const ov::Data> &data)
	{
		auto local_port = remote->GetLocalAddress()->Port();
		auto channel_id = remote->GetNativeHandle();

		auto stream_port_item = GetStreamPortItem(local_port);
		if (stream_port_item == nullptr)
		{
			logtc("Could not find StreamPortItem matching");  // %s", remote->ToString().CStr());
			return;
		}

		// UDP
		if (stream_port_item->IsClientConnected() == false)
		{
			if (OnConnected(remote, address_pair.GetRemoteAddress()) == false)
			{
				return;
			}
		}

		PushProvider::OnDataReceived(channel_id, data);
	}

	void MpegTsProvider::OnTimedOut(const std::shared_ptr<PushStream> &channel)
	{
		auto mpegts_stream = std::dynamic_pointer_cast<MpegTsStream>(channel);

		logti("The client has not sent data for %d seconds. Stream has been deleted: [%s/%s], remote: ",  //%s",
			  mpegts_stream->GetElapsedMsSinceLastReceived(),
			  mpegts_stream->GetApplicationName(), mpegts_stream->GetName().CStr());
		//mpegts_stream->GetClientSock()->ToString().CStr());

		auto stream_port_item = GetStreamPortItem(mpegts_stream->GetClientSock()->GetLocalAddress()->Port());
		if (stream_port_item == nullptr)
		{
			logtc("Could not find StreamPortItem matching");  // %s", remote->ToString().CStr());
			return;
		}

		stream_port_item->OnClientDisconnected();
	}

	// This function is not called by PhysicalPort when the protocol is udp (MPEGTS/UDP)
	void MpegTsProvider::OnDisconnected(const std::shared_ptr<ov::Socket> &remote,
										PhysicalPortDisconnectReason reason,
										const std::shared_ptr<const ov::Error> &error)
	{
		auto channel = GetChannel(remote->GetNativeHandle());
		if (channel == nullptr)
		{
			logte("Failed to find channel to delete stream (remote : ");  //%s)", remote->ToString().CStr());
			return;
		}

		logti("The MPEGTS client has disconnected: [%s/%s], remote: ",	//%s",
			  channel->GetApplicationName(), channel->GetName().CStr());
		//remote->ToString().CStr());

		PushProvider::OnChannelDeleted(remote->GetNativeHandle());
	}
}  // namespace pvd
