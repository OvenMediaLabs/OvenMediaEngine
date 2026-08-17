#pragma once

#include "../rtsp_library.h"
#include "rtp_track.h"
#include "rtp_packet_header.h"

#include <base/ovlibrary/data.h>

#include <memory>

class RtspServer;

class RtpTcpTrack : public RtpTrack
{
public:
    RtpTcpTrack(RtspServer& rtsp_server,
        cmn::MediaType media_type,
        cmn::MediaCodecId media_codec_id,
        uint32_t stream_id,
        uint8_t track_id,
        uint32_t clock_frequency,
        uint16_t rtp_channel,
        uint16_t rtcp_channel);

    bool AddPacket(uint8_t channel, const std::shared_ptr<std::vector<uint8_t>> &packet);

    template<typename U>
    static std::unique_ptr<U> Create(RtspServer &rtsp_server,
        cmn::MediaType media_type,
        cmn::MediaCodecId media_codec_id,
        uint32_t stream_id,
        uint8_t track_id,
        uint32_t clock_frequency,
        uint16_t rtp_channel,
        uint16_t rtcp_channel)
    {
        return std::make_unique<U>(rtsp_server, media_type, media_codec_id, stream_id, track_id, clock_frequency, rtp_channel, rtcp_channel);
    }

private:
    uint16_t rtp_channel_;
    uint16_t rtcp_channel_;
};