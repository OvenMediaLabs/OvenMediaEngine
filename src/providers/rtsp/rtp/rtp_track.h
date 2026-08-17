#pragma once

#include "../rtsp_library.h"
#include "rtp_packet_header.h"

#include <base/common_types.h>
#include <base/info/media_track.h>

#include <optional>

class RtspServer;

class RtpTrack
{
public:
    RtpTrack(RtspServer &rtsp_server_, cmn::MediaType media_type, cmn::MediaCodecId media_codec_id, uint32_t stream_id, uint8_t track_id, uint32_t clock_frequency);
    RtpTrack(const RtpTrack&) = delete;

    virtual ~RtpTrack() = default;

    bool AddRtpPacket(const std::shared_ptr<std::vector<uint8_t>> &rtp_packet);
    bool AddRtcpPacket(const std::shared_ptr<std::vector<uint8_t>> &rtcp_packet);

    uint8_t GetTrackId() const;

    virtual bool Initialize(const MediaTrack &media_track);

protected:
    virtual bool AddRtpPayload(const RtpPacketHeader &rtp_packet_header, uint8_t *rtp_payload, size_t rtp_payload_length) = 0;

protected:
    RtspServer &rtsp_server_;
    cmn::MediaType media_type_;
    cmn::MediaCodecId media_codec_id_; 
    uint32_t stream_id_;
    uint8_t track_id_;
    uint32_t clock_frequency_;
    bool first_packet_ = true;
    uint32_t first_timestamp_;
};