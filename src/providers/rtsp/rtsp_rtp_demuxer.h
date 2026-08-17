#pragma once

#include "rtp/rtp_tcp_track.h"

#include <base/ovlibrary/data.h>

#include <unordered_map>
#include <stdint.h>

class RtspRtpDemuxer
{
    enum class State
    {
        CheckMarker,
        GetChannel,
        GetSize,
        GatherPacket,
        ParsePacket,
    };

public:
    void AddInterleavedTrack(uint8_t channel, RtpTcpTrack* track);
    bool AppendMuxedData(const uint8_t* bytes, size_t length, size_t &bytes_consumed, bool &has_pending_data);

private:
    std::unordered_map<uint8_t, RtpTcpTrack*> interleaved_tracks_;
    State state_ = State::CheckMarker;
    uint8_t current_channel_;
    uint16_t current_packet_size_ = 0;
    bool current_packet_size_is_partial_ = false;
    std::shared_ptr<std::vector<uint8_t>> current_packet_;
};