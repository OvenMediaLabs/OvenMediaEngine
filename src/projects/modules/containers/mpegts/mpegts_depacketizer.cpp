//==============================================================================
//
//  MPEGTS Depacketizer
//
//  Created by Getroot
//  Copyright (c) 2020 AirenSoft. All rights reserved.
//
//==============================================================================
#include "mpegts_depacketizer.h"

#include <base/ovlibrary/bit_reader.h>

#define OV_LOG_TAG "MPEGTS_DEPACKETIZER"

namespace mpegts
{

	MpegTsDepacketizer::MpegTsDepacketizer()
	{
	}

	MpegTsDepacketizer::~MpegTsDepacketizer()
	{
	}

	bool MpegTsDepacketizer::AddPacket(const std::shared_ptr<const ov::Data> &packet)
	{
		_buffer->Append(packet);

		while (_buffer->GetLength() >= MPEGTS_MIN_PACKET_SIZE)
		{
			auto packet = std::make_shared<Packet>(_buffer);

			uint32_t parsed_length = packet->Parse();
			if (parsed_length == 0)
			{
				_buffer = _buffer->Subdata(MPEGTS_MIN_PACKET_SIZE);
				continue;
			}

			_buffer = _buffer->Subdata(parsed_length);

			if (AddPacket(packet) == false)
			{
				continue;
			}
		}

		return true;
	}

	bool MpegTsDepacketizer::AddPacket(const std::shared_ptr<Packet> &packet)
	{
		auto packet_type = GetPacketType(packet);

		if (packet_type == PacketType::UNSUPPORTED_SECTION || packet_type == PacketType::UNKNOWN)
		{
			// FFMPEG ususally sends PID 17 (DVB - SDT), but we don't use this table now
			logtt("Ignored unsupported or unknown MPEG-TS packets.(PID: %d)", packet->PacketIdentifier());
			return false;
		}

		// Check continuity counter
		// TODO(Getroot): Later, it can be used for jitter buffer to correct the UDP packet order
		if (packet->HasPayload())
		{
			auto it = _last_continuity_counter_map.find(packet->PacketIdentifier());
			if (it == _last_continuity_counter_map.end())
			{
				_last_continuity_counter_map.emplace(packet->PacketIdentifier(), packet->ContinuityCounter());
			}
			else
			{
				auto prev_counter = it->second;
				uint8_t expected_counter;

				if (prev_counter < 0x0f)
				{
					expected_counter = prev_counter + 1;
				}
				else
				{
					expected_counter = 0;
				}

				if (packet->ContinuityCounter() != expected_counter)
				{
					logtw("An out-of-order packet was received.(PID : %d Expected : %d, Received : %d",
						  packet->PacketIdentifier(), expected_counter, packet->ContinuityCounter());
				}

				_last_continuity_counter_map[packet->PacketIdentifier()] = packet->ContinuityCounter();
			}
		}

		// If PAT and PMT are completed, it doesn't need to parse anymore
		if (packet_type == PacketType::SUPPORTED_SECTION)
		{
			if (IsTrackInfoAvailable() == false)
			{
				logtt("Parsing section packet (PID: %d)", packet->PacketIdentifier());				
				return ParseSection(packet);
			}
		}
		else if (packet_type == PacketType::PES)
		{
			return ParsePes(packet);
		}
		else if (packet_type == PacketType::SECTION)
		{
			return ParseSection(packet);
		}

		return true;
	}

	bool MpegTsDepacketizer::IsTrackInfoAvailable()
	{
		return _pat_list_completed && _pmt_list_completed && _track_list_completed;
	}

	bool MpegTsDepacketizer::IsESAvailable()
	{
		return _es_list.size() > 0;
	}

	bool MpegTsDepacketizer::IsSectionAvailable()
	{
		return _section_list.size() > 0;
	}

	const std::shared_ptr<PAT> MpegTsDepacketizer::GetFirstPAT()
	{
		if (_pat_map.size() <= 0)
		{
			return nullptr;
		}

		auto it = _pat_map.begin();
		auto section = it->second;

		return section->GetPAT();
	}

	const std::shared_ptr<PAT> MpegTsDepacketizer::GetPAT(uint8_t program_number)
	{
		if (_pat_map.size() <= 0)
		{
			return nullptr;
		}

		auto it = _pat_map.find(program_number);
		if (it == _pat_map.end())
		{
			return nullptr;
		}

		auto section = it->second;

		return section->GetPAT();
	}

	bool MpegTsDepacketizer::GetPMTList(uint16_t program_num, std::vector<std::shared_ptr<Section>> *pmt_list)
	{
		auto range = _pmt_map.equal_range(program_num);

		std::transform(range.first, range.second, std::back_inserter(*pmt_list),
					   [](std::pair<uint16_t, std::shared_ptr<Section>> element) { return element.second; });

		return true;
	}

	bool MpegTsDepacketizer::GetTrackList(std::map<uint16_t, std::shared_ptr<MediaTrack>> *track_list)
	{
		*track_list = _media_tracks;
		return true;
	}

	const std::shared_ptr<Pes> MpegTsDepacketizer::PopES()
	{
		std::lock_guard<std::shared_mutex> lock(_es_list_lock);
		if (_es_list.size() == 0)
		{
			return nullptr;
		}

		auto es = _es_list.front();
		_es_list.pop();

		return es;
	}

	const std::shared_ptr<Section> MpegTsDepacketizer::PopSection()
	{
		std::lock_guard<std::shared_mutex> lock(_section_list_lock);
		if (_section_list.size() == 0)
		{
			return nullptr;
		}

		auto section = _section_list.front();
		_section_list.pop();

		return section;
	}

	PacketType MpegTsDepacketizer::GetPacketType(const std::shared_ptr<Packet> &packet)
	{
		switch (packet->PacketIdentifier())
		{
			// Well known PIDs
			case static_cast<uint16_t>(WellKnownPacketId::PAT):
				return PacketType::SUPPORTED_SECTION;

			case static_cast<uint16_t>(WellKnownPacketId::CAT):
			case static_cast<uint16_t>(WellKnownPacketId::TSDT):
			case static_cast<uint16_t>(WellKnownPacketId::NIT):
			case static_cast<uint16_t>(WellKnownPacketId::SDT):
			case static_cast<uint16_t>(WellKnownPacketId::NULL_PACKET):
				return PacketType::UNSUPPORTED_SECTION;
		}

		// PMT's PID are in PAT, PES's PID are in PMT
		// For quickly search they are stored in packet_type_table
		auto it = _packet_type_table.find(packet->PacketIdentifier());
		if (it == _packet_type_table.end())
		{
			return PacketType::UNKNOWN;
		}

		auto packet_type = it->second;

		return packet_type;
	}

	bool MpegTsDepacketizer::ParseSection(const std::shared_ptr<Packet> &packet)
	{
		BitReader bit_reader(packet->Payload(), packet->PayloadLength());

		// First packet of section, it means need to create new section draft and completed previous section
		if (packet->PayloadUnitStartIndicator())
		{
			// read pointer field - 8 bits
			auto pointer_field = bit_reader.ReadBytes<uint8_t>();

			// Check if there was an incomplete section
			auto prev_section = GetSectionDraft(packet->PacketIdentifier());
			if (prev_section != nullptr)
			{
				// Extract remaining data of previous section
				// 0~pointer_field bytes might be remained data of previous section
				auto previous_data = bit_reader.CurrentPosition();
				prev_section->AppendData(previous_data, pointer_field);

				if (prev_section->IsCompleted())
				{
					// Previous section completed
					if (CompleteSection(prev_section) == false)
					{
						logte("Could not complete section(PID: %d)", packet->PacketIdentifier());
						return false;
					}
				}
				else
				{
					// Somethind wrong
					logte("Could not complete section(PID: %d)", packet->PacketIdentifier());
				}
			}

			// Skip previous data
			bit_reader.SkipBytes(pointer_field);

			// Parsing new section
			while (bit_reader.BytesRemained() > 0)
			{
				auto new_section = std::make_shared<Section>(packet->PacketIdentifier());
				// There can be more than 2 sections
				auto consumed_bytes = new_section->AppendData(bit_reader.CurrentPosition(), bit_reader.BytesRemained());
				if (consumed_bytes == 0)
				{
					// Something wrong
					logte("Could not parse section(PID: %d)", packet->PacketIdentifier());
					return false;
				}

				bit_reader.SkipBytes(consumed_bytes);

				if (new_section->IsCompleted())
				{
					if (CompleteSection(new_section) == false)
					{
						logte("Could not complete section(PID: %d)", packet->PacketIdentifier());
						return false;
					}
				}
				else
				{
					// Store incompleted section
					SaveSectionDraft(new_section);
				}
			}
		}
		// There is only continuation of section data
		else
		{
			auto section = GetSectionDraft(packet->PacketIdentifier());
			if (section == nullptr)
			{
				// Something wrong
				logte("Could not find section(PID: %d) for depacketizing", packet->PacketIdentifier());
				return false;
			}

			// There is no new section in this packet, so all remained data has to be consumed
			auto consumed_length = section->AppendData(packet->Payload(), packet->PayloadLength());
			if (consumed_length != packet->PayloadLength())
			{
				return false;
			}

			if (section->IsCompleted())
			{
				return CompleteSection(section);
			}
		}

		return true;
	}

	bool MpegTsDepacketizer::ParsePes(const std::shared_ptr<Packet> &packet)
	{
		// First packet of pes, it has pes header
		if (packet->PayloadUnitStartIndicator())
		{
			// If there is previous PES, that is completed
			auto prev_pes = GetPesDraft(packet->PacketIdentifier());
			if (prev_pes != nullptr)
			{
				CompletePes(prev_pes);
			}

			auto pes = std::make_shared<Pes>(packet->PacketIdentifier());
			auto consumed_length = pes->AppendData(packet->Payload(), packet->PayloadLength());
			if (consumed_length != packet->PayloadLength())
			{
				logte("Something wrong with parsing PES");
				return false;
			}

			// If PES Packet Length of pes header is not zero, we can know if PES is completed
			if (pes->IsCompleted())
			{
				CompletePes(pes);
			}
			else
			{
				if (SavePesDraft(pes) == false)
				{
					return false;
				}
			}
		}
		else
		{
			auto pes = GetPesDraft(packet->PacketIdentifier());
			if (pes == nullptr)
			{
				// This can be called if the encoder sends faster than the server starts.
				// These packets can be ignored.
				logtt("Could not find the pes draft (PID: %d)", packet->PacketIdentifier());
				return false;
			}

			auto consumed_length = pes->AppendData(packet->Payload(), packet->PayloadLength());
			if (consumed_length != packet->PayloadLength())
			{
				logte("Something wrong with parsing PES");
				return false;
			}

			// If PES Packet Length of pes header is not zero, we can know if PES is completed
			if (pes->IsCompleted())
			{
				CompletePes(pes);
			}
		}

		return true;
	}

	const std::shared_ptr<Section> MpegTsDepacketizer::GetSectionDraft(uint16_t pid)
	{
		std::shared_lock<std::shared_mutex> lock(_section_draft_map_lock);

		auto it = _section_draft_map.find(pid);
		if (it == _section_draft_map.end())
		{
			return nullptr;
		}

		return it->second;
	}

	// incompleted section will be inserted
	bool MpegTsDepacketizer::SaveSectionDraft(const std::shared_ptr<Section> &section)
	{
		std::lock_guard<std::shared_mutex> lock(_section_draft_map_lock);

		_section_draft_map.emplace(section->PID(), section);

		return true;
	}

	// completed section will be removed
	bool MpegTsDepacketizer::CompleteSection(const std::shared_ptr<Section> &section)
	{
		std::lock_guard<std::shared_mutex> lock(_section_draft_map_lock);

		if (section->IsCompleted() == false)
		{
			return false;
		}

		// remove temporary section from section map
		_section_draft_map.erase(section->PID());

		// move
		if (section->TableId() == static_cast<uint8_t>(WellKnownTableId::PROGRAM_ASSOCIATION_SECTION))
		{
			auto pat = section->GetPAT();
			if (pat == nullptr)
			{
				return false;
			}

			// PAT
			_pat_map.emplace(pat->_program_num, section);
			// Reserve PMT's PID
			logtt("Registering PAT. PID: 0x%04X, PacketType::SUPPORTED_SECTION", pat->_program_map_pid);
			_packet_type_table.emplace(pat->_program_map_pid, PacketType::SUPPORTED_SECTION);

			// The last section for PAT
			// section number starts from 0
			if (_pat_map.size() - 1 == pat->_last_section_number)
			{
				_pat_list_completed = true;
			}
		}
		else if (section->TableId() == static_cast<uint8_t>(WellKnownTableId::PROGRAM_MAP_SECTION))
		{
			auto pmt = section->GetPMT();
			for (const auto &es_info : pmt->_es_info_list)
			{
				if(es_info->_stream_type == static_cast<uint8_t>(WellKnownStreamTypes::SCTE35))
				{
					logtt("Registering PMT. PID: 0x%04X, PacketType::SECTION", es_info->_elementary_pid);
					_packet_type_table.emplace(es_info->_elementary_pid, PacketType::SECTION);
				}
				else 
				{
					logtt("Registering PMT. PID: 0x%04X, PacketType::PES", es_info->_elementary_pid);
					_packet_type_table.emplace(es_info->_elementary_pid, PacketType::PES);
				}
			}

			// PMT
			_pmt_map.insert(std::pair<uint16_t, std::shared_ptr<Section>>(pmt->_table_id_extension, section));
			// ES Info
			for (const auto &es_info : pmt->_es_info_list)
			{
				_es_info_map.emplace(es_info->_elementary_pid, es_info);
			}

			if (_track_list_completed == false)
			{
				CreateTracks();
			}

			// Check if PMT is completed
			if (_pmt_map.count(pmt->_table_id_extension) - 1 == pmt->_last_section_number)
			{
				_completed_pmt_list.push_back(pmt->_table_id_extension);
			}

			// Check if all PMT is completed
			if (_pat_list_completed == true &&
				_pat_map.size() == _completed_pmt_list.size())
			{
				_pmt_list_completed = true;
			}
		}
		else
		{
			// Unsupported section
			logti("Ignored unsupported or unknown section (table id: %d)", section->TableId());
			return false;
		}

		return true;
	}

	const std::shared_ptr<Pes> MpegTsDepacketizer::GetPesDraft(uint16_t pid)
	{
		std::shared_lock<std::shared_mutex> lock(_pes_draft_map_lock);
		auto it = _pes_draft_map.find(pid);
		if (it == _pes_draft_map.end())
		{
			return nullptr;
		}

		return it->second;
	}

	// incompleted section will be inserted
	bool MpegTsDepacketizer::SavePesDraft(const std::shared_ptr<Pes> &pes)
	{
		std::lock_guard<std::shared_mutex> lock(_pes_draft_map_lock);
		_pes_draft_map.emplace(pes->PID(), pes);

		return true;
	}

	// process completed section and remove, extract a elementary stream (es)
	bool MpegTsDepacketizer::CompletePes(const std::shared_ptr<Pes> &pes)
	{
		std::unique_lock<std::shared_mutex> lock2(_pes_draft_map_lock);
		// if there is the pes in _pes_draft_map, remove it
		_pes_draft_map.erase(pes->PID());
		lock2.unlock();

		if (pes->SetEndOfData() == false)
		{
			return false;
		}

		// there is no media track, extracts it
		if (_media_tracks.find(pes->PID()) == _media_tracks.end())
		{
			logti("Unsupported PES has been received. (pid : %d stream_id : %d)", pes->PID(), pes->StreamId());
			return false;
		}

		std::unique_lock<std::shared_mutex> lock(_es_list_lock);
		_es_list.push(pes);
		lock.unlock();

		return true;
	}

	bool MpegTsDepacketizer::CreateTracks()
	{
		for (const auto &[pid, es_info] : _es_info_map)
		{
			auto track = std::make_shared<MediaTrack>();

			// Codec
			switch (es_info->_stream_type)
			{
				case static_cast<uint8_t>(WellKnownStreamTypes::H264):
					track->SetId(pid);
					track->SetMediaType(cmn::MediaType::Video);
					track->SetCodecId(cmn::MediaCodecId::H264);
					track->SetOriginBitstream(cmn::BitstreamFormat::H264_ANNEXB);
					track->SetTimeBase(1, TIMEBASE);
					track->SetVideoTimestampScale(TIMEBASE_DBL / 1000.0);
					break;

				case static_cast<uint8_t>(WellKnownStreamTypes::H265):
					track->SetId(pid);
					track->SetMediaType(cmn::MediaType::Video);
					track->SetCodecId(cmn::MediaCodecId::H265);
					track->SetOriginBitstream(cmn::BitstreamFormat::H265_ANNEXB);
					track->SetTimeBase(1, TIMEBASE);
					track->SetVideoTimestampScale(TIMEBASE_DBL / 1000.0);
					break;

				case static_cast<uint8_t>(WellKnownStreamTypes::AAC):
					track->SetId(pid);
					track->SetMediaType(cmn::MediaType::Audio);
					track->SetCodecId(cmn::MediaCodecId::Aac);
					track->SetOriginBitstream(cmn::BitstreamFormat::AAC_ADTS);
					track->SetTimeBase(1, TIMEBASE);
					break;

				case static_cast<uint8_t>(WellKnownStreamTypes::SCTE35):
					// SCTE-35 StreamType is used to Data Track in OvenMediaEngine.  Data track is created elsewhere for common use, so track is not created here.
					continue;

				default:
					// Doesn't support
					logtw("Unsupported stream_type has been received. (pid : %d stream_type : 0x%02x)", pid, es_info->_stream_type);
					continue;
			}

			_media_tracks.emplace(track->GetId(), track);
		}

		_track_list_completed = true;

		return true;
	}
}  // namespace mpegts