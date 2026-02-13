//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2024 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <base/common_types.h>
#include <base/mediarouter/mediarouter_application_interface.h>
#include <base/ovlibrary/url.h>
#include <base/publisher/publisher.h>
#include <modules/srt/srt.h>
#include <orchestrator/orchestrator.h>

#include "srt_application.h"
#include "srt_session.h"

namespace pub
{
	class SrtPublisher final : public Publisher, public PhysicalPortObserver
	{
	public:
		static std::shared_ptr<SrtPublisher> Create(const cfg::Server &server_config, const std::shared_ptr<MediaRouterInterface> &router);

		SrtPublisher(const cfg::Server &server_config, const std::shared_ptr<MediaRouterInterface> &router);
		~SrtPublisher() override final;

		bool Stop() override;

	private:
		bool Start() override;

		//--------------------------------------------------------------------
		// Implementation of Publisher
		//--------------------------------------------------------------------
		PublisherType GetPublisherType() const override
		{
			return PublisherType::Srt;
		}
		const char *GetPublisherName() const override
		{
			return "SRTPublisher";
		}

		bool OnCreateHost(const info::Host &host_info) override;
		bool OnDeleteHost(const info::Host &host_info) override;
		std::shared_ptr<Application> OnCreatePublisherApplication(const info::Application &application_info) override;
		bool OnDeletePublisherApplication(const std::shared_ptr<Application> &application) override;
		//--------------------------------------------------------------------

		//--------------------------------------------------------------------
		// Implementation of PhysicalPortObserver
		//--------------------------------------------------------------------
		void OnConnected(const std::shared_ptr<ov::Socket> &remote) override;
		void OnDataReceived(const std::shared_ptr<ov::Socket> &remote, const ov::SocketAddress &address, const std::shared_ptr<const ov::Data> &data) override;
		void OnDisconnected(const std::shared_ptr<ov::Socket> &remote, PhysicalPortDisconnectReason reason, const std::shared_ptr<const ov::Error> &error) override;
		//--------------------------------------------------------------------

	private:
		struct StreamMap
		{
			StreamMap(int port, const ov::String &stream_path)
				: port(port),
				  stream_path(stream_path)
			{
			}

			int port;
			ov::String stream_path;
		};

	private:
		void AddToDisconnect(const std::shared_ptr<ov::Socket> &remote);

		std::shared_ptr<SrtSession> GetSession(const std::shared_ptr<ov::Socket> &remote);

	private:
		std::mutex _physical_port_list_mutex;
		std::vector<std::shared_ptr<PhysicalPort>> _physical_port_list;

		modules::srt::StreamUrlResolver _stream_url_resolver;

		std::shared_mutex _session_map_mutex;
		std::map<session_id_t, std::shared_ptr<SrtSession>> _session_map;

		// When a request is made for a non-existent stream, the SRT socket connection is terminated.
		// However, since the client immediately attempts to reconnect, the socket connection is now terminated with a slight delay
		ov::DelayQueue _disconnect_timer{"SRTDiscnt"};
		// To minimize the use of mutex, use atomic variables before using mutex
		std::atomic<bool> _has_socket_list_to_disconnect{false};
		std::mutex _socket_list_to_disconnect_mutex;
		std::vector<std::shared_ptr<ov::Socket>> _socket_list_to_disconnect;
	};
}  // namespace pub
