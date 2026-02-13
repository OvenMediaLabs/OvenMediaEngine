#pragma once

#include "base/common_types.h"
#include "../rtp_packet.h"
#include "../rtcp_packet.h"

class RtcpSRGenerator
{
public:
    RtcpSRGenerator(uint32_t ssrc, uint32_t codec_rate);

	void AddRTPPacketInfo(const std::shared_ptr<RtpPacket> &rtp_packet);
	bool IsAvailableRtcpSRPacket() const;
	std::shared_ptr<RtcpPacket> PopRtcpSRPacket();
	
private:
	uint32_t GetElapsedTimeMSFromCreated();
	uint32_t GetElapsedTimeMSFromRtcpSRGenerated();

    uint32_t	_ssrc = 0;
	uint32_t	_last_timestamp = 0;
	uint64_t	_last_ntptime = 0;
    uint32_t    _rtcp_generated_count = 0;
    uint32_t    _packet_count = 0;
    uint32_t    _octec_count = 0;

	std::chrono::system_clock::time_point _created_time;
    std::chrono::system_clock::time_point _last_generated_time;
	uint32_t	_codec_rate = 1;

	std::shared_ptr<RtcpPacket>	_rtcp_packet = nullptr;
};