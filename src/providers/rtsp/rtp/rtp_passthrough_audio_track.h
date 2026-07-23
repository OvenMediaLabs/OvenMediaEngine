#pragma once

// A generic RTP track implementation which just forwards the whole RTP payload as media
template<typename T>
class RtpPassthroughAudioTrack : public T
{
public:
    using T::T;

    bool AddRtpPayload(const RtpPacketHeader &rtp_packet_header, uint8_t *rtp_payload, size_t rtp_payload_length) override
    {
        if (rtp_payload_length)
        {
            T::rtsp_server_.OnAudioData(T::stream_id_,
                T::track_id_,
                rtp_packet_header.timestamp_ - T::first_timestamp_,
                std::make_shared<std::vector<uint8_t>>(rtp_payload, rtp_payload + rtp_payload_length));
        }
        return true;
    }
};