//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <orchestrator/orchestrator.h>

#include "base/common_types.h"
#include "base/mediarouter/mediarouter_application_interface.h"
#include "base/ovlibrary/delay_queue.h"
#include "base/ovlibrary/message_thread.h"
#include "base/publisher/publisher.h"
#include "rtc_application.h"

class WebRtcPublisher : public pub::Publisher,
						public IcePortObserver,
						public RtcSignallingObserver
{
public:
	static std::shared_ptr<WebRtcPublisher> Create(const cfg::Server &server_config, const std::shared_ptr<MediaRouterInterface> &router);

	WebRtcPublisher(const cfg::Server &server_config, const std::shared_ptr<MediaRouterInterface> &router);
	~WebRtcPublisher() override;

	bool Stop() override;

	// IcePortObserver Implementation
	void OnStateChanged(IcePort &port, uint32_t session_id, IceConnectionState state, std::any user_data) override;
	void OnDataReceived(IcePort &port, uint32_t session_id, std::shared_ptr<const ov::Data> data, std::any user_data) override;

	// SignallingObserver Implementation
	std::shared_ptr<const SessionDescription> OnRequestOffer(const std::shared_ptr<http::svr::ws::WebSocketSession> &ws_session,
															 const info::VHostAppName &vhost_app_name, const ov::String &host_name, const ov::String &stream_name,
															 std::vector<RtcIceCandidate> *ice_candidates, bool &tcp_relay) override;
	bool OnAddRemoteDescription(const std::shared_ptr<http::svr::ws::WebSocketSession> &ws_session,
								const info::VHostAppName &vhost_app_name, const ov::String &host_name, const ov::String &stream_name,
								const std::shared_ptr<const SessionDescription> &offer_sdp,
								const std::shared_ptr<const SessionDescription> &answer_sdp) override;

	bool OnChangeRendition(const std::shared_ptr<http::svr::ws::WebSocketSession> &ws_session,
						   bool change_rendition, const ov::String &rendition_name, bool change_auto, bool &auto_abr,
						   const std::shared_ptr<const SessionDescription> &offer_sdp,
						   const std::shared_ptr<const SessionDescription> &answer_sdp) override;

	bool OnIceCandidate(const std::shared_ptr<http::svr::ws::WebSocketSession> &ws_session,
						const info::VHostAppName &vhost_app_name, const ov::String &host_name, const ov::String &stream_name,
						const std::shared_ptr<RtcIceCandidate> &candidate,
						const ov::String &username_fragment) override;

	bool OnStopCommand(const std::shared_ptr<http::svr::ws::WebSocketSession> &ws_session,
					   const info::VHostAppName &vhost_app_name, const ov::String &host_name, const ov::String &stream_name,
					   const std::shared_ptr<const SessionDescription> &offer_sdp,
					   const std::shared_ptr<const SessionDescription> &answer_sdp) override;

protected:
	bool StartSignallingServer(const cfg::Server &server_config, const cfg::bind::cmm::Webrtc &webrtc_bind_config);
	bool StartICEPorts(const cfg::Server &server_config, const cfg::bind::cmm::Webrtc &webrtc_bind_config);

private:
	enum class MessageCode : uint32_t
	{
		DISCONNECT_SESSION = 1,
	};

	enum class RequestStreamResult : int8_t
	{
		init = 0,
		local_success,
		local_failed,
		origin_success,
		origin_failed,
		transfer_completed,
	};

	bool Start() override;
	bool DisconnectSessionInternal(const std::shared_ptr<RtcSession> &session);

	//--------------------------------------------------------------------
	// Implementation of Publisher
	//--------------------------------------------------------------------
	PublisherType GetPublisherType() const override
	{
		return PublisherType::Webrtc;
	}
	const char *GetPublisherName() const override
	{
		return "WebRTC Publisher";
	}

	bool OnCreateHost(const info::Host &host_info) override;
	bool OnDeleteHost(const info::Host &host_info) override;
	bool OnUpdateCertificate(const info::Host &host_info) override;
	std::shared_ptr<pub::Application> OnCreatePublisherApplication(const info::Application &application_info) override;
	bool OnDeletePublisherApplication(const std::shared_ptr<pub::Application> &application) override;

	// This is a index used to send ICE Candidate in round-robin
	// WebRTC Publisher calculates the actual index by doing a modular operation, so it doesn't matter if overflow occurs
	std::atomic<uint32_t> _current_ice_candidate_index{0};

	std::shared_ptr<IcePort> _ice_port;
	std::shared_ptr<RtcSignallingServer> _signalling_server;

	// for special purpose log - Deprecated
	// ov::DelayQueue _timer;
};
