#include "rtp_track.h"
#include "../rtcp/rtcp.h"
#include "../rtsp_server.h"

RtpTrack::RtpTrack(RtspServer &rtsp_server,
    cmn::MediaType media_type,
    cmn::MediaCodecId media_codec_id,
    uint32_t stream_id,
    uint8_t track_id,
    uint32_t clock_frequency) : rtsp_server_(rtsp_server),
    media_type_(media_type),
    media_codec_id_(media_codec_id),
    stream_id_(stream_id),
    track_id_(track_id),
    clock_frequency_(clock_frequency)
{
}

bool RtpTrack::AddRtpPacket(const std::shared_ptr<std::vector<uint8_t>> &rtp_packet)
{
        const uint8_t (&rtp_header_bytes)[RtpPacketHeaderSize] = reinterpret_cast<const uint8_t(&)[RtpPacketHeaderSize]>(*rtp_packet->data());
        auto rtp_packet_header = RtpPacketHeaderFromData(rtp_header_bytes);
        if (first_packet_)
        {
            first_timestamp_ = rtp_packet_header.timestamp_;
            first_packet_ = false;
        }
        auto payload_offset = sizeof(RtpPacketHeader) + rtp_packet_header.csrc_count_ * sizeof(uint32_t);
        auto *rtp_payload = rtp_packet->data() + payload_offset;
        return AddRtpPayload(rtp_packet_header, rtp_payload, rtp_packet->size() - payload_offset);
}

bool RtpTrack::AddRtcpPacket(const std::shared_ptr<std::vector<uint8_t>> &rtcp_packet)
{
    constexpr uint32_t epoch_difference = 2208988800;
    const uint8_t (&rtcp_header_bytes)[RtcpPacketHeaderSize] = reinterpret_cast<const uint8_t(&)[RtcpPacketHeaderSize]>(*rtcp_packet->data());
    auto rtcp_packet_header = RtcpPacketHeaderFromData(rtcp_header_bytes);
    if (rtcp_packet_header.packet_type == RtcpPacketType::SenderReport)
    {
        RtcpSenderReport sender_report;
        static_cast<RtcpPacketHeader&>(sender_report) = rtcp_packet_header;
        const uint16_t rtcp_sender_report_size = (rtcp_packet_header.length_ + 1) * 4;
        OV_ASSERT2(rtcp_sender_report_size >= RtcpPacketHeaderSize);
        const uint16_t rtcp_sender_info_size = rtcp_sender_report_size - RtcpPacketHeaderSize;
        if (rtcp_sender_info_size >= 20)
        {
            const uint8_t *rtcp_sender_info = rtcp_packet->data() + RtcpPacketHeaderSize;
            const uint32_t ntp_timestamp_integer_part = ntohl(*reinterpret_cast<const uint32_t*>(rtcp_sender_info));
            rtcp_sender_info += 4;
            const uint32_t ntp_timestamp_fraction_part = ntohl(*reinterpret_cast<const uint32_t*>(rtcp_sender_info));
            rtcp_sender_info += 4;
            sender_report.rtp_timestamp_ = ntohl(*reinterpret_cast<const uint32_t*>(rtcp_sender_info));
            rtcp_sender_info += 4;
            sender_report.packet_count_ = ntohl(*reinterpret_cast<const uint32_t*>(rtcp_sender_info));
            rtcp_sender_info += 4;
            sender_report.octet_count_ = ntohl(*reinterpret_cast<const uint32_t*>(rtcp_sender_info));
            sender_report.ntp_timestamp_ = timeval
            {
                .tv_sec = ntp_timestamp_integer_part - epoch_difference,
                .tv_usec = static_cast<decltype(timeval::tv_usec)>((static_cast<uint64_t>(ntp_timestamp_fraction_part) * 1000000) >> 32)
            };
            rtsp_server_.OnRtcpSenderReport(stream_id_, track_id_, sender_report);
        }
    }
    return true;
}

uint8_t RtpTrack::GetTrackId() const
{
    return track_id_;
}

bool RtpTrack::Initialize(const MediaTrack &media_track)
{
    return true;
}