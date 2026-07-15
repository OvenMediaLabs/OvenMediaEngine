#include "rtsp_rtp_demuxer.h"

#include <base/ovlibrary/byte_ordering.h>

#define OV_LOG_TAG "RtspRtpDemuxer"

void RtspRtpDemuxer::AddInterleavedTrack(uint8_t channel, RtpTcpTrack* track)
{
    interleaved_tracks_.emplace(channel, track);
}

bool RtspRtpDemuxer::AppendMuxedData(const uint8_t* bytes, size_t length, size_t &bytes_consumed, bool &has_pending_data)
{
    size_t bytes_remaining = length;
    bool stop = false;
    bool result = true;
    while ((bytes_remaining || state_ == State::ParsePacket) && stop == false)
    {
        switch(state_)
        {
        case State::CheckMarker:
            {
                if (*bytes != '$')
                {
                    stop = true;
                    result = false;
                    break;
                }
                state_ = State::GetChannel;
                ++bytes;
                --bytes_remaining;
            }
            break;
        case State::GetChannel:
            current_channel_ = *bytes;
            if (interleaved_tracks_.find(current_channel_) == interleaved_tracks_.end())
            {
                logte("Invalid channel %u used in an interleaved RTP TCP stream", current_channel_);
                stop = true;
                result = false;
                break;
            }
            ++bytes;
            --bytes_remaining;
            state_ = State::GetSize;
            break;
        case State::GetSize:
            if (bytes_remaining == 1)
            {
                // If the sad scenario happens where the size remains on a packet boundary, then put the first byte in the higher byte,
                // thus imitating network byte order
                current_packet_size_ = static_cast<uint16_t>(*bytes) << 8;
                current_packet_size_is_partial_ = true;
                --bytes_remaining;
                ++bytes;
                break;
            }
            if (current_packet_size_is_partial_ == false)
            {
                current_packet_size_ = ntohs(*reinterpret_cast<const uint16_t*>(bytes));
                bytes_remaining -= 2;
                bytes += 2;
            }
            else
            {
                // We had the split case, so reconstruct the size
                current_packet_size_ |= *bytes;
                --bytes_remaining;
                ++bytes;
            }
            current_packet_ = std::make_shared<std::vector<uint8_t>>();
            state_ = State::GatherPacket;
            break;
        case State::GatherPacket:
            {
                auto bytes_to_consume = std::min(static_cast<size_t>(current_packet_size_- current_packet_->size()), bytes_remaining);
                current_packet_->insert(current_packet_->end(), bytes, bytes + bytes_to_consume);
                bytes_remaining -= bytes_to_consume;
                bytes += bytes_to_consume;
                if (current_packet_->size() == current_packet_size_)
                {
                    // Reset the current packet size here so that it is not forgotten to do that later
                    current_packet_size_ = 0;
                    current_packet_size_is_partial_ = false;
                    state_ = State::ParsePacket;
                }
            }
            break;
        case State::ParsePacket:
            {
                auto interleaved_track_iterator = interleaved_tracks_.find(current_channel_);
                if (interleaved_track_iterator == interleaved_tracks_.end())
                {
                    logte("Invalid channel %u used in an interleaved RTP TCP stream", current_channel_);
                    stop = true;
                    result = false;
                    break;
                }
                if (interleaved_track_iterator->second->AddPacket(current_channel_, current_packet_) == false)
                {
                    stop = true;
                    result = false;
                    break;
                }
                state_ = State::CheckMarker;
                // Once we have assembled a packet, the next interleaved data can be for a different channel, so stop here,
                // and let the demuxer lookup the correct stream
                stop = true;
            }
            break;
        }
    }
    bytes_consumed = length - bytes_remaining;
    has_pending_data = state_ != State::CheckMarker;
    return result;
}