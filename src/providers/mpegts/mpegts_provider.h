//==============================================================================
//
//  MPEGTS Provider
//
//  Created by Getroot
//  Copyright (c) 2020 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <base/ovlibrary/ovlibrary.h>
#include <base/ovsocket/ovsocket.h>
#include <orchestrator/orchestrator.h>
#include <stdint.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <functional>
#include <memory>
#include <thread>
#include <vector>

#include "base/provider/push_provider/provider.h"

namespace pvd
{
	class MpegTsStreamPortItem
	{
	public:
		// The application the port feeds, read in one piece
		struct Attachment
		{
			bool attached = false;
			// Changes on every attach and detach, so a channel built from an older attachment is not registered
			uint64_t generation = 0;
			info::VHostAppName vhost_app_name = info::VHostAppName::InvalidVHostAppName();
			ov::String stream_name;
		};

		// State : Init | Bound, Attached | Detached, Connected (`GetClientId()` is not `0`) | Disconnected
		MpegTsStreamPortItem(ov::SocketType scheme, uint16_t port, const std::vector<std::shared_ptr<PhysicalPort>> &physical_port_list)
			: _scheme(scheme),
			  _port(port),
			  _physical_port_list(physical_port_list)
		{
		}

		ov::SocketType GetScheme()
		{
			return _scheme;
		}

		uint16_t GetPortNumber()
		{
			return _port;
		}

		Attachment GetAttachment() const
		{
			std::scoped_lock lock(_state_mutex);
			return _attachment;
		}

		std::vector<std::shared_ptr<PhysicalPort>> GetPhysicalPortList()
		{
			std::scoped_lock lock(_physical_port_list_mutex);
			return _physical_port_list;
		}

		// Used by `MpegTsProvider::BindMpegTSPorts()` to attach `PhysicalPort`s to an item that
		// was constructed with an empty list during `MpegTsProvider::Start()`.
		void SetPhysicalPortList(const std::vector<std::shared_ptr<PhysicalPort>> &physical_port_list)
		{
			std::scoped_lock lock(_physical_port_list_mutex);
			_physical_port_list = physical_port_list;
		}

		void AttachToApplication(const info::VHostAppName &vhost_app_name, const ov::String &stream_name)
		{
			std::scoped_lock lock(_state_mutex);

			_attachment.vhost_app_name = vhost_app_name;
			_attachment.stream_name	   = stream_name;
			_attachment.generation++;
			_attachment.attached = true;
		}

		// Gives the port to `client_id` once `register_channel()` has registered its channel.
		// Fails while the port is detached or held, when `generation` is no longer current,
		// or when the registration fails.
		bool ConnectClient(channel_id_t client_id, uint64_t generation, const std::function<bool()> &register_channel)
		{
			std::scoped_lock lock(_state_mutex);

			if ((_attachment.attached == false) || (_attachment.generation != generation) ||
				(_client_id.load() != channel_id_t{}) || (register_channel() == false))
			{
				return false;
			}

			_client_id.store(client_id);
			return true;
		}

		// Returns the ID of the channel that held the port, or `0`
		channel_id_t DetachFromApplication()
		{
			std::scoped_lock lock(_state_mutex);

			_attachment.generation++;
			_attachment.attached = false;
			return _client_id.exchange(channel_id_t{});
		}

		// Frees the port only if `client_id` holds it and `is_owner()` agrees.
		// No channel can take the port while `is_owner()` runs.
		void DisconnectClient(channel_id_t client_id, const std::function<bool()> &is_owner = nullptr)
		{
			std::scoped_lock lock(_state_mutex);

			if ((_client_id.load() == client_id) && ((is_owner == nullptr) || is_owner()))
			{
				_client_id.store(channel_id_t{});
			}
		}

		bool IsAttached() const
		{
			std::scoped_lock lock(_state_mutex);
			return _attachment.attached;
		}

		// The ID of the channel holding the port, or `0` while the port is free
		channel_id_t GetClientId() const
		{
			return _client_id.load();
		}

	private:
		ov::SocketType _scheme = ov::SocketType::Udp;
		uint16_t _port = 0;
		mutable std::mutex _physical_port_list_mutex;
		std::vector<std::shared_ptr<PhysicalPort>> _physical_port_list;

		// Attaching, detaching, taking, and freeing the port hold this,
		// so neither the attachment nor the owner changes in between.
		mutable std::mutex _state_mutex;
		Attachment _attachment;
		std::atomic<channel_id_t> _client_id{};
	};

	class MpegTsProvider : public PushProvider, protected PhysicalPortObserver
	{
	public:
		// The silence timeout of a channel that has not published,
		// unless the operator set a positive `PacketSilenceTimeoutMs`.
		// UDP has no disconnect, so a sender that stops before completing PAT/PMT would otherwise hold the port forever.
		static constexpr time_t PRE_PUBLISH_PACKET_SILENCE_TIMEOUT_MS = 3000;

		static std::shared_ptr<MpegTsProvider> Create(const cfg::Server &server_config, const std::shared_ptr<MediaRouterInterface> &router);

		explicit MpegTsProvider(const cfg::Server &server_config, const std::shared_ptr<MediaRouterInterface> &router);
		~MpegTsProvider() override;

		bool Start() override;
		bool Bind() override;
		bool Stop() override;

		//--------------------------------------------------------------------
		// Implementation of Provider's pure virtual functions
		//--------------------------------------------------------------------
		ProviderStreamDirection GetProviderStreamDirection() const override
		{
			return ProviderStreamDirection::Push;
		}

		ProviderType GetProviderType() const override
		{
			return ProviderType::Mpegts;
		}

		const char *GetProviderName() const override
		{
			return "MPEGTSProvider";
		}

	protected:
		struct StreamInfo
		{
			StreamInfo(info::VHostAppName vhost_app_name, ov::String stream_name)
				: vhost_app_name(std::move(vhost_app_name)),
				  stream_name(std::move(stream_name))
			{
			}

			info::VHostAppName vhost_app_name;
			ov::String stream_name;
		};

		// Creates physical ports for each item in `_stream_port_map` (populated by `Start()`)
		// and starts listening. Called from `Bind()`.
		bool BindMpegTSPorts();

		// Protected so a test can check which channel holds a port
		std::shared_ptr<MpegTsStreamPortItem> GetStreamPortItem(uint16_t local_port);

		//--------------------------------------------------------------------
		// Implementation of Provider's virtual functions
		//--------------------------------------------------------------------
		bool OnCreateHost(const info::Host &host_info) override;
		bool OnDeleteHost(const info::Host &host_info) override;
		std::shared_ptr<Application> OnCreateProviderApplication(const info::Application &application_info) override;
		bool OnDeleteProviderApplication(const std::shared_ptr<Application> &application) override;

		//--------------------------------------------------------------------
		// Implementation of PushProvider's virtual functions
		//--------------------------------------------------------------------
		void OnTimedOut(const std::shared_ptr<PushStream> &channel) override;

		//--------------------------------------------------------------------
		// Implementation of PhysicalPortObserver
		//--------------------------------------------------------------------
		void OnConnected(const std::shared_ptr<ov::Socket> &remote) override;
		bool OnConnected(const std::shared_ptr<ov::Socket> &remote, const ov::SocketAddressPair &address_pair);

		void OnDatagramReceived(const std::shared_ptr<ov::Socket> &remote,
								const ov::SocketAddressPair &address_pair,
								const std::shared_ptr<const ov::Data> &data) override;

		void OnDisconnected(const std::shared_ptr<ov::Socket> &remote,
							PhysicalPortDisconnectReason reason,
							const std::shared_ptr<const ov::Error> &error) override;

	private:
		std::shared_ptr<MpegTsStreamPortItem> GetDetachedStreamPortItem();

		// Deletes the channel that held a port of a deleted application, once no `OnDataReceived()` is inside it.
		// The channel is reserved for deletion first, so a call that was inside cannot publish it afterwards.
		// A channel still in use at `deadline` is left in `_channels`, where the silence timeout deletes it.
		void DeleteUnpublishedChannel(channel_id_t channel_id, std::chrono::steady_clock::time_point deadline);

		std::shared_mutex _stream_port_map_lock;
		std::map<uint16_t, std::shared_ptr<MpegTsStreamPortItem>> _stream_port_map;
	};
}  // namespace pvd
