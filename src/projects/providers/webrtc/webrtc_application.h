//==============================================================================
//
//  WebRTC Provider
//
//  Created by Getroot
//  Copyright (c) 2021 AirenSoft. All rights reserved.
//
//==============================================================================

#pragma once

#include "base/common_types.h"
#include "base/provider/push_provider/application.h"
#include "base/provider/push_provider/stream.h"

#include "base/ovcrypto/certificate.h"
#include "modules/ice/ice_port.h"
#include "modules/rtc_signalling/rtc_signalling.h"

namespace pvd
{
	class WebRTCApplication : public pvd::PushApplication
	{
	public:
		static std::shared_ptr<WebRTCApplication> Create(const std::shared_ptr<pvd::PushProvider> &provider, const info::Application &application_info, const std::shared_ptr<Certificate> &certificate, const std::shared_ptr<IcePort> &ice_port, const std::shared_ptr<RtcSignallingServer> &rtc_signalling);

		explicit WebRTCApplication(const std::shared_ptr<pvd::PushProvider> &provider, const info::Application &application_info, const std::shared_ptr<Certificate> &certificate, const std::shared_ptr<IcePort> &ice_port, const std::shared_ptr<RtcSignallingServer> &rtc_signalling);
		~WebRTCApplication() override = default;

		bool Start() override;
		bool Stop() override;

		std::shared_ptr<const SessionDescription> GetOfferSDP();
		std::shared_ptr<SessionDescription> CreateAnswerSDP(const std::shared_ptr<const SessionDescription> &offer_sdp, const ov::String &local_ufrag, const std::set<IceCandidate> &ice_candidates);
		
	private:
		std::shared_ptr<SessionDescription> CreateOfferSDP();

		std::shared_ptr<IcePort> _ice_port = nullptr;
		std::shared_ptr<RtcSignallingServer> _rtc_signalling = nullptr;
		
		std::shared_ptr<SessionDescription> _offer_sdp = nullptr;
		std::shared_ptr<Certificate> _certificate = nullptr;
	};
}