//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2023 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <base/ovlibrary/ovlibrary.h>
#include "../rtp_packet.h"
#include "transport_cc.h"

// https://datatracker.ietf.org/doc/html/draft-holmer-rmcat-transport-wide-cc-extensions-01

// RTP header extension format
//  0                   1                   2                   3
//  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |       0xBE    |    0xDE       |           length=1            |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |  ID   | L=1   |transport-wide sequence number | zero padding  |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+


// RTCP Message Format
//  0                   1                   2                   3
//  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |V=2|P|  FMT=15 |    PT=205     |           length              |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |                     SSRC of packet sender                     |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |                      SSRC of media source                     |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |      base sequence number     |      packet status count      |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |                 reference time                | fb pkt. count |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |          packet chunk         |         packet chunk          |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// .                                                               .
// .                                                               .
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |         packet chunk          |  recv delta   |  recv delta   |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// .                                                               .
// .                                                               .
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |           recv delta          |  recv delta   | zero padding  |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+

class RtcpTransportCcFeedbackGenerator
{
public:
	RtcpTransportCcFeedbackGenerator(uint8_t extension_id, uint32_t sender_ssrc);

	bool AddReceivedRtpPacket(const std::shared_ptr<RtpPacket> &packet);
	bool HasElapsedSinceLastTransportCc(uint32_t milliseconds);
	std::shared_ptr<RtcpPacket> GenerateTransportCcMessage();
	uint32_t GetPacketStatusCount() const
	{
		if (_transport_cc == nullptr)
		{
			return 0;
		}

		return _transport_cc->GetPacketStatusCount();
	}

private:
	std::chrono::high_resolution_clock::time_point _created_time;
	uint8_t _extension_id = 0;
	uint32_t _sender_ssrc = 0;
	uint32_t _last_media_ssrc = 0;

	bool _is_first_packet = true;
	uint16_t _last_wide_sequence_number = 0;

	uint8_t _fb_pkt_count = 0;

	std::chrono::high_resolution_clock::time_point _last_reference_time; // multiples of 64ms, now() - created_time base
	std::chrono::high_resolution_clock::time_point _last_rtp_received_time;
	std::shared_ptr<TransportCc> _transport_cc = nullptr;

	std::chrono::high_resolution_clock::time_point _last_report_time;
};