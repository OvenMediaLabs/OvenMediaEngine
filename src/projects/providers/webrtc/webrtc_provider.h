//==============================================================================
//
//  WebRTC Provider
//
//  Created by Getroot
//  Copyright (c) 2021 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include "base/ovlibrary/ovlibrary.h"
#include "base/provider/push_provider/provider.h"
#include "modules/ice/ice_port_manager.h"
#include "modules/rtc_signalling/rtc_signalling.h"
#include "modules/whip/whip_server.h"
#include "orchestrator/orchestrator.h"
#include "webrtc_stream.h"

namespace pvd
{
	class WebRTCProvider : public PushProvider,
						   public IcePortObserver,
						   public RtcSignallingObserver,
						   public WhipObserver
	{
	public:
		static std::shared_ptr<WebRTCProvider> Create(const cfg::Server &server_config, const std::shared_ptr<MediaRouterInterface> &router);

		explicit WebRTCProvider(const cfg::Server &server_config, const std::shared_ptr<MediaRouterInterface> &router);
		~WebRTCProvider() override;

		bool Start() override;
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
			return ProviderType::WebRTC;
		}

		const char *GetProviderName() const override
		{
			return "WebRTCProvider";
		}
		//--------------------------------------------------------------------

		//--------------------------------------------------------------------
		// IcePortObserver Implementation
		//--------------------------------------------------------------------
		void OnStateChanged(IcePort &port, uint32_t session_id, IceConnectionState state, std::any user_data) override;
		void OnDataReceived(IcePort &port, uint32_t session_id, std::shared_ptr<const ov::Data> data, std::any user_data) override;
		//--------------------------------------------------------------------

		//--------------------------------------------------------------------
		// SignallingObserver Implementation
		//--------------------------------------------------------------------
		std::shared_ptr<const SessionDescription> OnRequestOffer(const std::shared_ptr<http::svr::ws::WebSocketSession> &ws_session,
																 const info::VHostAppName &vhost_app_name, const ov::String &host_name, const ov::String &stream_name,
																 std::vector<RtcIceCandidate> *ice_candidates, bool &tcp_relay) override;
		bool OnAddRemoteDescription(const std::shared_ptr<http::svr::ws::WebSocketSession> &ws_session,
									const info::VHostAppName &vhost_app_name, const ov::String &host_name, const ov::String &stream_name,
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
		//--------------------------------------------------------------------

		//--------------------------------------------------------------------
		// WhipObserver Implementation
		//--------------------------------------------------------------------
		WhipObserver::Answer OnSdpOffer(const std::shared_ptr<const http::svr::HttpRequest> &request,
										const std::shared_ptr<const SessionDescription> &offer_sdp) override;

		WhipObserver::Answer OnTrickleCandidate(const std::shared_ptr<const http::svr::HttpRequest> &request,
												const ov::String &session_id,
												const ov::String &if_match,
												const std::shared_ptr<const SessionDescription> &patch) override;

		WhipObserver::Answer OnSessionDelete(const std::shared_ptr<const http::svr::HttpRequest> &request,
												const ov::String &session_key) override;
		//--------------------------------------------------------------------

	protected:
		bool StartSignallingServers(const cfg::Server &server_config, const cfg::bind::cmm::Webrtc &webrtc_bind_config);
		bool StartICEPorts(const cfg::Server &server_config, const cfg::bind::cmm::Webrtc &webrtc_bind_config);

	private:
		std::shared_ptr<Certificate> CreateCertificate();
		std::shared_ptr<Certificate> GetCertificate();

		bool RegisterStreamToSessionKeyStreamMap(const std::shared_ptr<WebRTCStream> &stream);
		bool UnRegisterStreamToSessionKeyStreamMap(const ov::String &session_key);
		std::shared_ptr<WebRTCStream> GetStreamBySessionKey(const ov::String &session_key);

		//--------------------------------------------------------------------
		// Implementation of Provider's virtual functions
		//--------------------------------------------------------------------
		bool OnCreateHost(const info::Host &host_info) override;
		bool OnDeleteHost(const info::Host &host_info) override;
		bool OnUpdateCertificate(const info::Host &host_info) override;

		std::shared_ptr<pvd::Application> OnCreateProviderApplication(const info::Application &application_info) override;
		bool OnDeleteProviderApplication(const std::shared_ptr<pvd::Application> &application) override;
		//--------------------------------------------------------------------

		// This is a index used to send ICE Candidate in round-robin
		// WebRTC Provider calculates the actual index by doing a modular operation, so it doesn't matter if overflow occurs
		std::atomic<uint32_t> _current_ice_candidate_index{0};

		std::shared_ptr<IcePort> _ice_port = nullptr;
		std::shared_ptr<RtcSignallingServer> _signalling_server = nullptr;
		std::shared_ptr<WhipServer> _whip_server = nullptr;
		std::shared_ptr<Certificate> _certificate = nullptr;

		std::mutex _stream_lock;

		mutable std::shared_mutex _session_key_stream_map_guard;
		// Key: stream_key / Value: WebRTCStream
		std::map<ov::String, std::shared_ptr<WebRTCStream>> _session_key_stream_map;
	};
}  // namespace pvd