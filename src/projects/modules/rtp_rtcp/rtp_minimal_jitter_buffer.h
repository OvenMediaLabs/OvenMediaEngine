#pragma once

#include "base/ovlibrary/ovlibrary.h"
#include "rtp_packet.h"
#include <unordered_map>

#define DEFAULT_AUDIO_MAX_BUFFERING_TIME_MS	200

// It only corrects unordered packet for rfc3551
class RtpMinimalJitterBuffer
{
public:
	bool InsertPacket(const std::shared_ptr<RtpPacket> &packet);
	bool HasAvailablePacket();
	std::shared_ptr<RtpPacket> PopAvailablePacket();
	
private:
	uint16_t _next_sequence_number OV_GUARDED_BY(_lock) = 0;
	uint32_t _max_buffering_time_ms = DEFAULT_AUDIO_MAX_BUFFERING_TIME_MS;

	struct RtpPacketBox
	{
		RtpPacketBox(uint64_t packaging_time_ms, std::shared_ptr<RtpPacket> packet)
		{
			_packaging_time_ms = packaging_time_ms;
			_packet = packet;
		}

		uint64_t _packaging_time_ms = 0;
		std::shared_ptr<RtpPacket> _packet = nullptr;
	};

	// sequence number : RtpFrameInfo
	// it should be ordered, so use std::map
	std::map<uint16_t, std::shared_ptr<RtpPacketBox>> _rtp_packets OV_GUARDED_BY(_lock);

	mutable ov::Mutex _lock;
};