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

#include <chrono>
#include <thread>

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
		auto &server_config			 = GetServerConfig();
		auto &ip_list				 = server_config.GetIPList();
		auto &mpegts_provider_config = server_config.GetBind().GetProviders().GetMpegts();
		auto &port_config			 = mpegts_provider_config.GetPort();
		auto socket_type			 = port_config.GetSocketType();

		std::vector<ov::String> address_string_list;
		auto physical_port_manager = PhysicalPortManager::GetInstance();

		// MPEG-TS uses one stream per port, so multiple workers have no effect.
		if (mpegts_provider_config.GetWorkerCount() > 1)
		{
			logtw("MPEG-TS WorkerCount is set to %d, but only 1 worker is used per port",
				  mpegts_provider_config.GetWorkerCount());
		}

		// Snapshot to bind. Items were populated by `Start()` with empty
		// `_physical_port_list`; here we create the physical ports and attach them via
		// `SetPhysicalPortList()`.
		decltype(_stream_port_map) stream_port_map_snapshot;
		{
			std::shared_lock lock(_stream_port_map_lock);
			stream_port_map_snapshot = _stream_port_map;
		}

		bool result = true;

		for (auto &[port, stream_port_item] : stream_port_map_snapshot)
		{
			std::vector<ov::SocketAddress> address_list;

			try
			{
				address_list = ov::SocketAddress::Create(ip_list, port);
			}
			catch (const ov::Error &e)
			{
				logte("Could not create socket address: %s", e.What());
				result = false;
				break;
			}

			std::vector<std::shared_ptr<PhysicalPort>> physical_port_list;

			for (const auto &address : address_list)
			{
				auto physical_port = physical_port_manager->CreatePort("MPEGTS", socket_type, address, 1);

				if (physical_port == nullptr)
				{
					logte("Could not initialize physical port for MPEG-TS server: %s/%s", address.ToString().CStr(), ov::StringFromSocketType(socket_type));
					result = false;
					break;
				}

				address_string_list.emplace_back(address.ToString());
				physical_port->AddObserver(this);
				physical_port_list.emplace_back(physical_port);
			}

			stream_port_item->SetPhysicalPortList(physical_port_list);

			if (result == false)
			{
				break;
			}
		}

		if (result)
		{
			auto socket_type_string = ov::StringFromSocketType(socket_type);
			ov::String suffix		= ov::String::FormatString("/%s, ", socket_type_string);
			logti("%s is listening on %s/%s", GetProviderName(), ov::String::Join(address_string_list, suffix).CStr(), socket_type_string);
			return true;
		}

		// Rollback: tear down any ports we managed to create.
		for (const auto &[port, stream_port_item] : stream_port_map_snapshot)
		{
			auto physical_port_list = stream_port_item->GetPhysicalPortList();

			for (auto &physical_port : physical_port_list)
			{
				physical_port->RemoveObserver(this);
				physical_port_manager->DeletePort(physical_port);
			}
			stream_port_item->SetPhysicalPortList({});
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
		// Infrastructure setup only - populate `_stream_port_map` with metadata-only items
		// so `OnCreateProviderApplication()` can attach apps to ports during the StartServer
		// notification pass. Physical port binding is deferred to `Bind()`.
		auto &server_config			 = GetServerConfig();
		auto &mpegts_provider_config = server_config.GetBind().GetProviders().GetMpegts();

		if (mpegts_provider_config.IsParsed() == false)
		{
			logtw("%s is disabled by configuration", GetProviderName());
			return true;
		}

		auto &port_config	   = mpegts_provider_config.GetPort();
		auto &port_list_config = port_config.GetPortList();
		auto socket_type	   = port_config.GetSocketType();

		std::map<uint16_t, std::shared_ptr<MpegTsStreamPortItem>> stream_port_map;

		for (const auto &port : port_list_config)
		{
			stream_port_map.emplace(port,
				std::make_shared<MpegTsStreamPortItem>(socket_type, port, std::vector<std::shared_ptr<PhysicalPort>>{}));
		}

		{
			std::unique_lock lock_guard{_stream_port_map_lock};
			_stream_port_map = std::move(stream_port_map);
		}

		return Provider::Start();
	}

	bool MpegTsProvider::Bind()
	{
		auto &server_config = GetServerConfig();

		if (server_config.GetBind().GetProviders().GetMpegts().IsParsed() == false)
		{
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
		decltype(_stream_port_map) stream_port_map;
		{
			std::scoped_lock lock(_stream_port_map_lock);
			stream_port_map = std::move(_stream_port_map);
		}

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
		std::vector<channel_id_t> client_id_list;

		{
			std::shared_lock<std::shared_mutex> lock(_stream_port_map_lock);

			for (const auto &item : _stream_port_map)
			{
				auto &stream_port_item = item.second;
				const auto attachment  = stream_port_item->GetAttachment();
				if (attachment.attached && (attachment.vhost_app_name == application->GetVHostAppName()))
				{
					auto client_id = stream_port_item->DetachFromApplication();
					if (client_id != channel_id_t{})
					{
						client_id_list.push_back(client_id);
					}
				}
			}
		}

		auto result = PushProvider::OnDeleteProviderApplication(application);

		// Waiting for a channel blocks every application create and delete, so all ports share one bound
		constexpr auto WAIT_LIMIT = std::chrono::seconds(3);
		const auto deadline		  = std::chrono::steady_clock::now() + WAIT_LIMIT;

		// `PushProvider::OnDeleteProviderApplication()` deletes only the channels that joined the application.
		// A port's channel that has not published yet is deleted here,
		// or it would keep its ID and the port could not register it again.
		for (const auto &client_id : client_id_list)
		{
			DeleteUnpublishedChannel(client_id, deadline);
		}

		return result;
	}

	void MpegTsProvider::DeleteUnpublishedChannel(channel_id_t channel_id, std::chrono::steady_clock::time_point deadline)
	{
		// An `OnDataReceived()` that is inside the channel may complete PAT/PMT and publish it.
		// After the application is recreated, it would join the new one by name with no entry in `_channels`,
		// and nothing would ever delete it.
		// Application create and delete are serialized, so no recreated application exists during the wait,
		// and a reserved channel takes no more data.
		// A channel still in use at `deadline` stays in `_channels`.
		// If the call publishes it into a recreated application, it receives nothing from the port,
		// and the silence timeout deletes it.
		while (true)
		{
			// The port is detached, so no newer channel can take this ID while this runs.
			// A missing channel was already deleted: a published one with the application,
			// or by the timeout, which reserves it the same way.
			auto channel = GetChannel(channel_id);
			if (channel == nullptr)
			{
				return;
			}

			if (channel->TryBeginReaping(channel->GetSilenceState()))
			{
				PushProvider::OnChannelDeleted(channel);
				return;
			}

			if (std::chrono::steady_clock::now() >= deadline)
			{
				logtw("Channel %" PRIu64 " is still receiving data, leaving it to the silence timeout", channel_id.GetValue());
				return;
			}

			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
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

		if (OnConnected(remote, ov::SocketAddressPair(*remote->GetLocalAddress(), *remote->GetRemoteAddress())) == false)
		{
			remote->Close();
		}
	}

	bool MpegTsProvider::OnConnected(const std::shared_ptr<ov::Socket> &remote, const ov::SocketAddressPair &address_pair)
	{
		auto local_port		  = remote->GetLocalAddress()->Port();
		auto channel_id		  = GetChannelId(remote);

		auto stream_port_item = GetStreamPortItem(local_port);
		if (stream_port_item == nullptr)
		{
			return false;
		}

		const auto attachment = stream_port_item->GetAttachment();
		if (attachment.attached == false)
		{
			return false;
		}

		// The port takes one client at a time:
		// another TCP connection, or a sender on its other socket (IPv4/IPv6), is refused.
		if (stream_port_item->GetClientId() != channel_id_t{})
		{
			return false;
		}

		// A channel that still holds this ID refuses the registration below,
		// so this skips building a stream that would only be thrown away.
		if (GetChannel(channel_id) != nullptr)
		{
			return false;
		}

		auto stream = MpegTsStream::Create(
			StreamSourceType::Mpegts, channel_id,
			attachment.vhost_app_name, attachment.stream_name,
			remote, address_pair, 0, GetSharedPtrAs<PushProvider>());

		// A timed-out channel keeps its ID until it is deleted, so the ID may still be registered.
		// Only this port registers the IDs of its sockets, and `ConnectClient()` holds the port's lock here,
		// so an ID found free stays free until this channel is registered.
		auto register_channel = [&]() {
			return (GetChannel(channel_id) == nullptr) && PushProvider::OnChannelCreated(channel_id, stream);
		};

		// Fails while the port is taken, the attachment has changed, or the ID is still registered.
		// A later datagram tries again; a TCP connection is closed.
		if (stream_port_item->ConnectClient(channel_id, attachment.generation, register_channel) == false)
		{
			return false;
		}

		logti("A MPEG-TS client has connected");  // %s", remote->ToString().CStr());

		// Apply the configured `PacketSilenceTimeoutMs` to the pre-publish window, now that the application is known.
		// `JoinStream()` replaces the timeout when the stream publishes.
		stream->ApplyConfiguredPacketSilenceTimeoutMs(attachment.vhost_app_name);
		if (stream->GetPacketSilenceTimeoutMs() == 0)
		{
			stream->SetPacketSilenceTimeoutMs(PRE_PUBLISH_PACKET_SILENCE_TIMEOUT_MS);
		}

		return true;
	}

	void MpegTsProvider::OnDatagramReceived(const std::shared_ptr<ov::Socket> &remote,
											const ov::SocketAddressPair &address_pair,
											const std::shared_ptr<const ov::Data> &data)
	{
		auto local_port = remote->GetLocalAddress()->Port();
		auto channel_id = GetChannelId(remote);

		auto stream_port_item = GetStreamPortItem(local_port);
		if (stream_port_item == nullptr)
		{
			logtc("Could not find StreamPortItem matching");  // %s", remote->ToString().CStr());
			return;
		}

		// UDP
		if (stream_port_item->GetClientId() != channel_id)
		{
			if (OnConnected(remote, address_pair) == false)
			{
				return;
			}
		}

		PushProvider::OnDataReceived(channel_id, data);
	}

	void MpegTsProvider::OnTimedOut(const std::shared_ptr<PushStream> &channel)
	{
		auto mpegts_stream = std::dynamic_pointer_cast<MpegTsStream>(channel);

		logti("The client has not sent data for %" PRIdMAX " ms. Stream has been deleted: [%s/%s], remote: ",  //%s",
			  static_cast<intmax_t>(mpegts_stream->GetElapsedMsSinceLastReceived()),
			  mpegts_stream->GetApplicationName(), mpegts_stream->GetName().CStr());
		//mpegts_stream->GetClientSock()->ToString().CStr());

		auto stream_port_item = GetStreamPortItem(mpegts_stream->GetClientSock()->GetLocalAddress()->Port());
		if (stream_port_item == nullptr)
		{
			logtc("Could not find StreamPortItem matching");  // %s", remote->ToString().CStr());
			return;
		}

		const auto channel_id = channel->GetChannelId();

		// A UDP port's channels share the ID of its socket, so a channel deleted with its application
		// may time out after a newer channel took the port with the same ID.
		stream_port_item->DisconnectClient(channel_id, [&]() { return GetChannel(channel_id) == channel; });
	}

	// This function is not called by PhysicalPort when the protocol is udp (MPEGTS/UDP)
	void MpegTsProvider::OnDisconnected(const std::shared_ptr<ov::Socket> &remote,
										PhysicalPortDisconnectReason reason,
										const std::shared_ptr<const ov::Error> &error)
	{
		const auto channel_id = GetChannelId(remote);
		const auto channel	  = GetChannel(channel_id);
		if (channel == nullptr)
		{
			// It was probably rejected in `OnConnected()`
			logtt("Failed to find channel to delete stream (remote : %s)", remote->ToString().CStr());
			return;
		}

		logti("The MPEGTS client has disconnected: [%s/%s], remote: ",	//%s",
			  channel->GetApplicationName(), channel->GetName().CStr());
		//remote->ToString().CStr());

		PushProvider::OnChannelDeleted(channel);

		auto stream_port_item = GetStreamPortItem(remote->GetLocalAddress()->Port());
		if (stream_port_item != nullptr)
		{
			stream_port_item->DisconnectClient(channel_id);
		}
	}
}  // namespace pvd
