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

	// Atomically: if at least `milliseconds` elapsed since the last report,
	// build and return the feedback packet (resetting the interval); else nullptr.
	std::shared_ptr<RtcpPacket> GenerateTransportCcMessageIfElapsed(uint32_t milliseconds);
	uint32_t GetPacketStatusCount() const
	{
		ov::LockGuard<ov::Mutex> lock(_lock);
		if (_transport_cc == nullptr)
		{
			return 0;
		}

		return _transport_cc->GetPacketStatusCount();
	}

private:
	std::chrono::steady_clock::time_point _created_time;
	uint8_t _extension_id = 0;
	uint32_t _sender_ssrc = 0;
	uint32_t _last_media_ssrc OV_GUARDED_BY(_lock) = 0;

	bool _is_first_packet OV_GUARDED_BY(_lock) = true;
	uint16_t _last_wide_sequence_number OV_GUARDED_BY(_lock) = 0;

	uint8_t _fb_pkt_count OV_GUARDED_BY(_lock) = 0;

	std::chrono::steady_clock::time_point _last_reference_time OV_GUARDED_BY(_lock); // multiples of 64ms, now() - created_time base
	std::chrono::steady_clock::time_point _last_rtp_received_time OV_GUARDED_BY(_lock);
	std::shared_ptr<TransportCc> _transport_cc OV_GUARDED_BY(_lock) = nullptr;

	std::chrono::steady_clock::time_point _last_report_time OV_GUARDED_BY(_lock);

	mutable ov::Mutex _lock;
};