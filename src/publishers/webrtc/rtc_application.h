//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <base/common_types.h>
#include <base/info/session.h>
#include <base/ovcrypto/certificate.h>
#include <base/publisher/application.h>
#include <base/publisher/publisher.h>
#include <modules/ice/ice_port.h>
#include <modules/rtc_signalling/rtc_signalling.h>
#include <modules/rtp_rtcp/rtcp_packet.h>

#include "rtc_stream.h"

class RtcApplication final : public pub::Application
{
public:
	static std::shared_ptr<RtcApplication> Create(const std::shared_ptr<pub::Publisher> &publisher,
												  const info::Application &application_info,
												  const std::shared_ptr<IcePort> &ice_port,
												  const std::shared_ptr<RtcSignallingServer> &rtc_signalling);
	RtcApplication(const std::shared_ptr<pub::Publisher> &publisher,
				   const info::Application &application_info,
				   const std::shared_ptr<IcePort> &ice_port,
				   const std::shared_ptr<RtcSignallingServer> &rtc_signalling);
	~RtcApplication() final;

	std::shared_ptr<Certificate> GetCertificate();

private:
	bool Start() override;
	bool Stop() override;

	// Application Implementation
	std::shared_ptr<pub::Stream> CreateStream(const std::shared_ptr<info::Stream> &info, uint32_t worker_count) override;
	bool DeleteStream(const std::shared_ptr<info::Stream> &info) override;

	std::shared_ptr<IcePort> _ice_port;
	std::shared_ptr<RtcSignallingServer> _rtc_signalling;
	std::shared_ptr<Certificate> _certificate;
};