//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2024 AirenSoft. All rights reserved.
//
//==============================================================================
#include "mpegts_packetizer.h"
#include "mpegts_private.h"

#include "descriptors/metadata_pointer.h"
#include "descriptors/metadata.h"

namespace mpegts
{
    Packetizer::Packetizer()
    {
        // default config
        _config.mpegts_packet_size = 188;
    }

    Packetizer::Packetizer(const Config &config)
    {
        _config = config;
    }

    Packetizer::~Packetizer()
    {
    }

    bool Packetizer::AddSink(const std::shared_ptr<PacketizerSink> &sink)
    {
        _sinks.push_back(sink);
        return true;
    }

    bool Packetizer::RemoveSink(const std::shared_ptr<PacketizerSink> &sink)
    {
        auto it = std::find(_sinks.begin(), _sinks.end(), sink);
        if (it != _sinks.end())
        {
            _sinks.erase(it);
        }
        return true;
    }

	bool Packetizer::IsSupportedCodec(cmn::MediaCodecId codec_id)
	{
		return GetElementaryStreamTypeByCodecId(codec_id) != WellKnownStreamTypes::None;
	}

    bool Packetizer::AddTrack(const std::shared_ptr<const MediaTrack> &media_track)
    {
        if (IsStarted())
        {
            return false;
        }

		if (media_track->GetMediaType() == cmn::MediaType::Audio || media_track->GetMediaType() == cmn::MediaType::Video)
		{
			auto stream_type = GetElementaryStreamTypeByCodecId(media_track->GetCodecId());
			if (stream_type == WellKnownStreamTypes::None)
			{
				logte("%s codec is not supported", cmn::GetCodecIdString(media_track->GetCodecId()));
				return false;
			}
		}

        _media_tracks.emplace(media_track->GetId(), media_track);

        if (media_track->GetMediaType() == cmn::MediaType::Video)
        {
            _first_video_frame_received = false;
            _has_video = true;
        }

        return true;
    }

    std::shared_ptr<const MediaTrack> Packetizer::GetMediaTrack(uint32_t track_id) const
    {
        auto it = _media_tracks.find(track_id);
        if (it != _media_tracks.end())
        {
            return it->second;
        }

        return nullptr;
    }

    bool Packetizer::Start()
    {
        if (_started)
        {
            return false;
        }

        _pat_packet = BuildPatPacket();
        if (_pat_packet == nullptr)
        {
            return false;
        }

        _pmt_packet = BuildPmtPacket();
        if (_pmt_packet == nullptr)
        {
            return false;
        }

#if 0
        // Debug
        logtt("PAT : %s", _pat_packet->GetData()->ToHexString().CStr());
        logtt("PMT : %s", _pmt_packet->GetData()->ToHexString().CStr());
#endif

        BroadcastPsi();

        _started = true;
        return true;
    }

    bool Packetizer::IsStarted() const
    {
        return _started;
    }

    bool Packetizer::Stop()
    {
        _started = false;
        return true;
    }
    
    bool Packetizer::AppendFrame(const std::shared_ptr<const MediaPacket> &media_packet)
    {
        if (IsStarted() == false)
        {
            return false;
        }

        if (_has_video == true && _first_video_frame_received == false && media_packet->GetMediaType() == cmn::MediaType::Video)
        {
            if (media_packet->IsKeyFrame() == false)
            {
				logtw("First video frame is not key frame so it has been dropped : %s", media_packet->GetInfoString().CStr());
                return false;
            }

            _first_video_frame_received = true;
        }

        // logtt("AppendFrame track_id %u, media_type %s, pts %lld, dts %lld", media_packet->GetTrackId(), cmn::GetMediaTypeString(media_packet->GetMediaType()), media_packet->GetPts(), media_packet->GetDts());

        auto track = GetMediaTrack(media_packet->GetTrackId());
        if (track == nullptr)
        {
            return false;
        }

		if (media_packet->GetMediaType() == cmn::MediaType::Data && media_packet->GetBitstreamFormat() != cmn::BitstreamFormat::ID3v2)
		{
			return false;
		}

        auto pid = GetElementaryPid(media_packet->GetTrackId());
        auto pes = Pes::Build(pid, track, media_packet);
        if (pes == nullptr)
        {
            return false;
        }

        auto continuity_counter = GetNextContinuityCounter(pid);
        bool has_pcr = (pid == _pmt._pcr_pid);
        auto packets = Packet::Build(pes, has_pcr, continuity_counter);
        if (packets.empty())
        {
            return false;
        }

#if 0
        // debug print
        logtt("------------------------------------------------------------------------");
        logtt("Track(%u) / MediaPacket(%u)", media_packet->GetTrackId(), media_packet->GetDataLength());
        for (const auto &packet : packets)
        {
            logtt("%s", packet->ToDebugString().CStr());
        }
#endif

        IncreaseContinuityCounter(pid, static_cast<uint8_t>(packets.size() - 1));

        BroadcastFrame(media_packet, packets);

        return true;
    }

    const Packetizer::Config &Packetizer::GetConfig() const
    {
        return _config;
    }

    std::shared_ptr<mpegts::Packet> Packetizer::BuildPatPacket()
    {
		_pat._table_id_extension = 1;
		_pat._version_number = 0;
		_pat._current_next_indicator = true;
		_pat._section_number = 0;
		_pat._last_section_number = 0;

        _pat._program_num = PROGRAM_NUMBER;
        _pat._program_map_pid = PMT_PID;

        auto pat_section = Section::Build(_pat);
        if (pat_section == nullptr)
        {
            return nullptr;
        }

        auto pat_packet = Packet::Build(pat_section, 0);
        if (pat_packet == nullptr)
        {
            return nullptr;
        }

        return pat_packet;
    }

	std::shared_ptr<mpegts::Descriptor> Packetizer::BuildID3MetadataPointerDescriptor()
	{
		auto descriptor = std::make_shared<MetadataPointerDescriptor>();

		descriptor->SetMetadataApplicationFormatIdentifier(0x49443320); // "ID3 "
		descriptor->SetMetadataFormatIdentifier(0x49443320); // "ID3 "
		descriptor->SetMetadataServiceId(0);
		descriptor->SetMetadataLocatorRecordFlag(0);
		descriptor->SetMpegCarriageFlag(0);
		descriptor->SetProgramNumber(PROGRAM_NUMBER);

		return descriptor;
	}

	std::shared_ptr<mpegts::Descriptor> Packetizer::BuildID3MetadataDescriptor()
	{
		auto descriptor = std::make_shared<MetadataDescriptor>();

		descriptor->SetMetadataApplicationFormatIdentifier(0x49443320); // "ID3 "
		descriptor->SetMetadataFormatIdentifier(0x49443320); // "ID3 "
		descriptor->SetMetadataServiceId(0);
		descriptor->SetDecoderConfigFlags(0);
		descriptor->SetDsmCcFlag(0);

		return descriptor;
	}

    std::shared_ptr<mpegts::Packet> Packetizer::BuildPmtPacket()
    {
		_pmt._table_id_extension = PROGRAM_NUMBER;
		_pmt._version_number = 0;
		_pmt._current_next_indicator = true;
		_pmt._section_number = 0;
		_pmt._last_section_number = 0;

		auto program_info_descriptor = BuildID3MetadataPointerDescriptor();
		auto program_info_descriptor_data = program_info_descriptor->Build();

		if (program_info_descriptor_data != nullptr)
		{
			_pmt._program_info_length = program_info_descriptor_data->GetLength();
			_pmt._program_descriptors.emplace_back(program_info_descriptor);
		}
		
		// Media tracks first
        for (const auto &track_it : _media_tracks)
        {
            auto track = track_it.second;
			if (track->GetMediaType() == cmn::MediaType::Data)
			{
				// Add it later
				continue;
			}

            auto es_info = std::make_shared<ESInfo>();

            es_info->_stream_type = static_cast<uint8_t>(GetElementaryStreamTypeByCodecId(track->GetCodecId()));
            if (es_info->_stream_type == 0)
            {
                logte("Could not get stream type for track(%u)", track->GetId());
                return nullptr;
            }

            auto elementary_pid = GetElementaryPid(track->GetId());
            if (_pmt._pcr_pid == 0x1FFF && track->GetMediaType() == cmn::MediaType::Video)
            {
                _pmt._pcr_pid = elementary_pid;
            }
            
            es_info->_elementary_pid = elementary_pid;
            es_info->_es_info_length = 0; // descriptors length

            _pmt._es_info_list.push_back(es_info);
        }

        if (_pmt._pcr_pid == 0x1FFF && GetFirstElementaryPid() != 0)
        {
            _pmt._pcr_pid = GetFirstElementaryPid();
        }

		// Data tracks
		for (const auto &track_it : _media_tracks)
		{
			auto track = track_it.second;
			if (track->GetMediaType() != cmn::MediaType::Data)
			{
				continue;
			}

			auto es_info = std::make_shared<ESInfo>();
			auto es_descriptor = BuildID3MetadataDescriptor();
			auto es_descriptor_data = es_descriptor->Build();

			if (es_descriptor_data != nullptr)
			{
				es_info->_stream_type = static_cast<uint8_t>(WellKnownStreamTypes::METADATA_CARRIED_IN_PES);
				es_info->_elementary_pid = GetElementaryPid(track->GetId());
				es_info->_es_info_length = es_descriptor_data->GetLength();
				es_info->_es_descriptors.emplace_back(es_descriptor);
				_pmt._es_info_list.push_back(es_info);
			}
		}

        auto pmt_section = Section::Build(PMT_PID, _pmt);
        if (pmt_section == nullptr)
        {
            return nullptr;
        }

        auto pmt_packet = Packet::Build(pmt_section, 0);
        if (pmt_packet == nullptr)
        {
            return nullptr;
        }

        return pmt_packet;
    }

    uint16_t Packetizer::GetElementaryPid(uint32_t track_id)
    {
        auto it = _pids.find(track_id);
        if (it != _pids.end())
        {
            return it->second;
        }

        // elementary pid starts from 0x101
        auto pid = static_cast<uint16_t>(MIN_ELEMENTARY_PID + _pids.size());
        _pids.emplace(track_id, pid);

        return pid;
    }

    uint16_t Packetizer::GetFirstElementaryPid() const
    {
        if (_pids.empty())
        {
            return 0;
        }

        return _pids.begin()->second;
    }

    WellKnownStreamTypes Packetizer::GetElementaryStreamTypeByCodecId(cmn::MediaCodecId codec_id)
    {
		switch (codec_id)
		{
			case cmn::MediaCodecId::H264:
				return WellKnownStreamTypes::H264;
			case cmn::MediaCodecId::H265:
				return WellKnownStreamTypes::H265;
			case cmn::MediaCodecId::Aac:
				return WellKnownStreamTypes::AAC;
			default:
				return WellKnownStreamTypes::None;
		}

        return WellKnownStreamTypes::None;
    }

    uint8_t Packetizer::GetNextContinuityCounter(uint16_t pid)
    {
        auto it = _continuity_counters.find(pid);
        if (it == _continuity_counters.end())
        {
            _continuity_counters[pid] = 0;
            return 0;
        }

        auto &continuity_counter = it->second;
        continuity_counter = (continuity_counter + 1) % 16;

        return continuity_counter;
    }

    void Packetizer::IncreaseContinuityCounter(uint16_t pid, uint32_t add_count)
    {
        auto it = _continuity_counters.find(pid);
        if (it != _continuity_counters.end())
        {
            uint32_t continuity_counter = it->second;
            it->second = (continuity_counter + add_count) % 16;
        }
    }

    void Packetizer::BroadcastPsi()
    {
        auto tracks = std::vector<std::shared_ptr<const MediaTrack>>();
        for (const auto &track_it : _media_tracks)
        {
            tracks.push_back(track_it.second);
        }

        for (const auto &sink : _sinks)
        {
            sink->OnPsi(tracks, {_pat_packet, _pmt_packet});
        }
    }

    void Packetizer::BroadcastFrame(const std::shared_ptr<const MediaPacket> &media_packet, const std::vector<std::shared_ptr<mpegts::Packet>> &pes_packets)
    {
        for (const auto &sink : _sinks)
        {
            sink->OnFrame(media_packet, pes_packets);
        }
    }
}