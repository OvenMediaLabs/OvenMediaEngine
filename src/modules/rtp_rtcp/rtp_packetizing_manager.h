#pragma once

#include "rtp_rtcp_defines.h"
#include "rtp_packet.h"

class RtpPacketizingManager
{
public:
	static std::shared_ptr<RtpPacketizingManager> Create(cmn::MediaCodecId type);

	virtual ~RtpPacketizingManager()
	{
	}

	virtual size_t SetPayloadData(size_t max_payload_len, size_t last_packet_reduction_len, const RTPVideoTypeHeader *rtp_type_header, FrameType frame_type,
									const uint8_t *payload_data, size_t payload_size, const FragmentationHeader *fragmentation) = 0;

	virtual bool NextPacket(RtpPacket *packet) = 0;
};

