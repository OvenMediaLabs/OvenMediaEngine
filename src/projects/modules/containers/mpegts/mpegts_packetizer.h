//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2024 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <base/info/media_track.h>
#include <base/mediarouter/media_buffer.h>

#include "mpegts_common.h"
#include "mpegts_section.h"
#include "mpegts_pes.h"
#include "mpegts_packet.h"

namespace mpegts
{
    class PacketizerSink
    {
    public:
        virtual ~PacketizerSink() = default;
        // PAT, PMT, ...
        virtual void OnPsi(const std::vector<std::shared_ptr<const MediaTrack>> &tracks, const std::vector<std::shared_ptr<mpegts::Packet>> &psi_packets) = 0;
        // PES packets for a frame
        virtual void OnFrame(const std::shared_ptr<const MediaPacket> &media_packet, const std::vector<std::shared_ptr<mpegts::Packet>> &pes_packets) = 0;
    };

    // PAT, PMT, PES, PES, PES, ...
    class Packetizer
    {
    public:
        struct Config
        {
            uint32_t mpegts_packet_size = 188;
        };

        Packetizer();
        Packetizer(const Config &config);
        ~Packetizer();

        bool AddSink(const std::shared_ptr<PacketizerSink> &sink);
        bool RemoveSink(const std::shared_ptr<PacketizerSink> &sink);

		static bool IsSupportedCodec(cmn::MediaCodecId codec_id);

        bool AddTrack(const std::shared_ptr<const MediaTrack> &media_track);

        bool Start();
        bool IsStarted() const;
        bool Stop();
        
        bool AppendFrame(const std::shared_ptr<const MediaPacket> &media_packet);

    private:
        const Config &GetConfig() const;

        std::shared_ptr<mpegts::Packet> BuildPatPacket();
        std::shared_ptr<mpegts::Packet> BuildPmtPacket();


		std::shared_ptr<mpegts::Descriptor> BuildID3MetadataPointerDescriptor();
		std::shared_ptr<mpegts::Descriptor> BuildID3MetadataDescriptor();

        uint16_t GetElementaryPid(uint32_t track_id);
        uint16_t GetFirstElementaryPid() const;
        static WellKnownStreamTypes GetElementaryStreamTypeByCodecId(cmn::MediaCodecId codec_id);
        uint8_t GetNextContinuityCounter(uint16_t pid);
        void IncreaseContinuityCounter(uint16_t pid, uint32_t add_count);

        std::shared_ptr<const MediaTrack> GetMediaTrack(uint32_t track_id) const;

        void BroadcastPsi();
        void BroadcastFrame(const std::shared_ptr<const MediaPacket> &media_packet, const std::vector<std::shared_ptr<mpegts::Packet>> &pes_packets);

        Config _config;
        bool _started = false;

        bool _has_video = false;
        bool _first_video_frame_received = false;

        std::vector<std::shared_ptr<PacketizerSink>> _sinks;

        // track id : media track
        std::map<uint32_t, std::shared_ptr<const MediaTrack>> _media_tracks;

        // PAT
        PAT _pat;
        std::shared_ptr<mpegts::Packet> _pat_packet;
        // PMT
        PMT _pmt;
        std::shared_ptr<mpegts::Packet> _pmt_packet;

        // PID
        // track id : pid
        std::map<uint32_t, uint16_t> _pids;

        // Discontinuity Counter
        // pid : continuity counter (0~15)
        std::map<uint16_t, uint8_t> _continuity_counters;
    };
}