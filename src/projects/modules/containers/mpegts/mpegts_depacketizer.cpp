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
#include <base/ovlibrary/clock.h>

#define OV_LOG_TAG "MPEGTS_DEPACKETIZER"

// Every log in this file is prefixed with the owning stream's name path (set via `SetNamePath()`).
#define OV_LOG_PREFIX_FORMAT "[%s] "
#define OV_LOG_PREFIX_VALUE _name_path.CStr()

namespace mpegts
{

	MpegTsDepacketizer::MpegTsDepacketizer()
	{
		// Cache whether debug logging is enabled for this tag so the per-packet continuity-break
		// detail can be skipped entirely (no argument formatting) on a hot, lossy path.
		_debug_enabled = ov_log_get_enabled(OV_LOG_TAG, OVLogLevelDebug);
	}

	MpegTsDepacketizer::~MpegTsDepacketizer()
	{
	}

	void MpegTsDepacketizer::EnablePacketReordering()
	{
		// Must be enabled before any data is processed: otherwise the byte buffer may already hold a
		// partial datagram and the continuity map is primed, while the reorder buffer would start
		// fresh and second-guess the counter order.
		OV_ASSERT2((_buffer->GetLength() == 0) && _last_continuity_counter_map.empty());

		if (_reorder_buffer == nullptr)
		{
			_reorder_buffer = std::make_unique<DatagramReorderBuffer>();
		}
	}

	bool MpegTsDepacketizer::AddPacket(const std::shared_ptr<const ov::Data> &datagram)
	{
		if (_reorder_buffer != nullptr)
		{
			std::vector<std::shared_ptr<const ov::Data>> ordered;
			const auto gap_loss = _reorder_buffer->Enqueue(datagram, &ordered);

			if (gap_loss > 0)
			{
				logad("MPEG-TS reorder: declared %zu datagram gap(s) lost (depth/timeout flush)", gap_loss);
			}

			bool result = true;
			for (const auto &d : ordered)
			{
				if (ProcessDatagram(d) == false)
				{
					result = false;
				}
			}

			return result;
		}

		return ProcessDatagram(datagram);
	}

	bool MpegTsDepacketizer::ProcessDatagram(const std::shared_ptr<const ov::Data> &datagram)
	{
		_buffer->Append(datagram);

		while (_buffer->GetLength() >= MPEGTS_MIN_PACKET_SIZE)
		{
			auto packet			   = std::make_shared<Packet>(_buffer);

			uint32_t parsed_length = packet->Parse();
			if (parsed_length == 0)
			{
				// Parse can fail either because the grid is misaligned (bad sync byte) or because an
				// otherwise-aligned packet failed a later field check (e.g. transport_error_indicator).
				// If we are locked to the 188-byte grid and the leading byte is a sync byte, `data[0]` is
				// a genuine boundary and this is an aligned-but-corrupt packet: drop exactly one packet
				// and stay aligned (this preserves the grid for the TEI packets a lossy link produces).
				if (_synced && (_buffer->GetDataAs<uint8_t>()[0] == MPEGTS_SYNC_BYTE))
				{
					logad("Dropped an aligned but unparsable MPEG-TS packet (sync byte present); staying on the grid");
					_buffer = _buffer->Subdata(MPEGTS_MIN_PACKET_SIZE);
					continue;
				}

				// Not locked to the grid (or the leading byte is not a sync byte):
				// the grid is (or may be) misaligned. Rather than blindly skipping a fixed 188 bytes,
				// scan for the next sync byte confirmed by another sync byte one/two packet lengths ahead,
				// and drop the bytes before it.
				if (_synced)
				{
					logad("Lost MPEG-TS sync; scanning to resynchronize");
				}

				_synced				 = false;

				size_t resync_offset = FindResyncOffset();
				if (resync_offset == 0)
				{
					// No boundary can be confirmed yet (a candidate needs another sync byte a packet length ahead).
					// Best-effort minimal advance: keep from the earliest sync byte and wait for more data.
					// That byte may be a stray payload 0x47 rather than a true boundary;
					// if so the next append re-runs Parse/resync from there and converges. If
					// there is no sync byte at all, the buffer is pure garbage and is dropped.
					const uint8_t *data = _buffer->GetDataAs<uint8_t>();
					const size_t length = _buffer->GetLength();
					size_t keep_from	= length;
					for (size_t i = 1; i < length; i++)
					{
						if (data[i] == MPEGTS_SYNC_BYTE)
						{
							keep_from = i;
							break;
						}
					}

					if (keep_from < length)
					{
						logad("Resync: no confirmed boundary yet; dropped %zu byte(s), waiting for more data", keep_from);
						_buffer = _buffer->Subdata(keep_from);
					}
					else
					{
						logad("Resync: no sync byte in %zu byte(s); dropped the whole buffer", length);
						_buffer = std::make_shared<ov::Data>();
					}
					break;
				}

				// Candidate boundary from the double/triple-sync scan.
				// Do NOT mark synced yet: only a successful Parse at this offset confirms it,
				// so a false payload-0x47 lock re-scans on the next parse failure
				// instead of being trusted by the aligned-but-corrupt path.
				logad("Resync: candidate boundary at offset %zu; dropped %zu leading byte(s)", resync_offset, resync_offset);
				_buffer = _buffer->Subdata(resync_offset);
				continue;
			}

			// A full packet parsed: data now points at the next 188-byte boundary.
			_synced = true;
			_buffer = _buffer->Subdata(parsed_length);

			// Resolves to the Packet overload (the per-packet sink), NOT the datagram entry point,
			// so this must never re-enter the reorder buffer.
			if (AddPacket(packet) == false)
			{
				continue;
			}
		}

		return true;
	}

	size_t MpegTsDepacketizer::FindResyncOffset() const
	{
		const auto data	  = _buffer->GetDataAs<uint8_t>();
		const auto length = _buffer->GetLength();

		// A boundary is confirmed by a second sync byte one packet length ahead (double-sync),
		// and by a third one two packet lengths ahead when the buffer is long enough.
		// This avoids relocking onto a 0x47 byte that merely appears inside a packet payload.
		for (size_t offset = 1; offset + MPEGTS_MIN_PACKET_SIZE < length; offset++)
		{
			if (data[offset] != MPEGTS_SYNC_BYTE)
			{
				continue;
			}

			if (data[offset + MPEGTS_MIN_PACKET_SIZE] != MPEGTS_SYNC_BYTE)
			{
				continue;
			}

			const auto third = offset + (2 * MPEGTS_MIN_PACKET_SIZE);

			if ((third < length) && (data[third] != MPEGTS_SYNC_BYTE))
			{
				continue;
			}

			return offset;
		}

		return 0;
	}

	bool MpegTsDepacketizer::AddPacket(const std::shared_ptr<Packet> &packet)
	{
		auto packet_type = GetPacketType(packet);

		if (packet_type == PacketType::UNSUPPORTED_SECTION || packet_type == PacketType::UNKNOWN)
		{
			// FFMPEG ususally sends PID 17 (DVB - SDT), but we don't use this table now
			logat("Ignored unsupported or unknown MPEG-TS packets.(PID: %d)", packet->PacketIdentifier());
			return false;
		}

		// Continuity handling. This hardening applies to every transport (not only the reordering path):
		// a continuity break on a reliable transport (SRT/TCP) originates at the encoder,
		// and discarding the partial frame there is also correct.
		// Duplicate handling is inert on transports that never duplicate.
		const auto pid = packet->PacketIdentifier();

		// A signalled adaptation-field discontinuity may be carried on a payload packet OR on an
		// adaptation-field-only packet (e.g. a PCR discontinuity).
		// Honor it regardless of payload: drop any partial assembly and reset continuity tracking
		// so the next packet re-establishes the baseline without a spurious continuity-break warning.
		// The discontinuity_indicator only exists when the adaptation field actually carries its flag
		// byte (length > 0); guard on it to avoid trusting a defaulted bit.
		if (packet->HasAdaptationField() &&
			(packet->GetAdaptationField()._length > 0) &&
			packet->GetAdaptationField()._discontinuity_indicator)
		{
			DiscardPesDraft(pid);
			DiscardSectionDraft(pid);
			_last_continuity_counter_map.erase(pid);
			_cc_duplicate_seen.erase(pid);
		}

		// The continuity counter only advances on packets that carry a payload (adaptation-field-only
		// packets do not increment it).
		if (packet->HasPayload())
		{
			const uint8_t counter = packet->ContinuityCounter();

			auto it = _last_continuity_counter_map.find(pid);
			if (it != _last_continuity_counter_map.end())
			{
				const uint8_t prev_counter = it->second;
				const uint8_t expected_counter = (prev_counter + 1) & 0x0F;

				if (counter == expected_counter)
				{
					_cc_duplicate_seen.erase(pid);
				}
				else if ((counter == prev_counter) && (_cc_duplicate_seen.count(pid) == 0))
				{
					// A packet may legally be duplicated once with the same continuity counter and
					// identical content. Drop the duplicate so its payload is not appended twice.
					// (With only a 4-bit counter, losing exactly a multiple of 16 payload packets is
					// indistinguishable from a duplicate; that residual ambiguity is unavoidable.)
					_cc_duplicate_seen.insert(pid);
					logad("Dropped duplicate MPEG-TS packet (PID: %d CC: %d)", pid, counter);
					return true;
				}
				else
				{
					// Genuine continuity break (loss, a second consecutive same-counter packet, or corruption):
					// drop any partial assembly so nothing corrupt is forwarded.
					// A lossy link can hit this on nearly every packet, so the per-occurrence detail goes to debug only
					// (guarded by `_debug_enabled` so the arguments are not even formatted when debug is off),
					// and the warning is rate-limited to roughly one line per interval.
					if (_debug_enabled)
					{
						logad("MPEG-TS continuity break (PID: %d Expected: %d Received: %d); discarding partial data",
							  pid, expected_counter, counter);
					}

					LogContinuityBreak();

					DiscardPesDraft(pid);
					DiscardSectionDraft(pid);
					_cc_duplicate_seen.erase(pid);
				}
			}

			_last_continuity_counter_map[pid] = counter;
		}

		// If PAT and PMT are completed, it doesn't need to parse anymore
		if (packet_type == PacketType::SUPPORTED_SECTION)
		{
			if (IsTrackInfoAvailable() == false)
			{
				logat("Parsing section packet (PID: %d)", packet->PacketIdentifier());
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
						logae("Could not complete section(PID: %d)", packet->PacketIdentifier());
						return false;
					}
				}
				else
				{
					// Somethind wrong
					logae("Could not complete section(PID: %d)", packet->PacketIdentifier());
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
					logae("Could not parse section(PID: %d)", packet->PacketIdentifier());
					return false;
				}

				bit_reader.SkipBytes(consumed_bytes);

				if (new_section->IsCompleted())
				{
					if (CompleteSection(new_section) == false)
					{
						logae("Could not complete section(PID: %d)", packet->PacketIdentifier());
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
				// Expected when the section draft for this PID was discarded on a continuity break:
				// the following continuation packets then have no draft to append to. This is normal recovery
				// (a warning was already logged at the break), so trace it rather than error.
				logad("No section draft for continuation (PID: %d); likely discarded on a continuity break", packet->PacketIdentifier());
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
				logae("Something wrong with parsing PES");
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
				logat("Could not find the pes draft (PID: %d)", packet->PacketIdentifier());
				return false;
			}

			auto consumed_length = pes->AppendData(packet->Payload(), packet->PayloadLength());
			if (consumed_length != packet->PayloadLength())
			{
				logae("Something wrong with parsing PES");
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
			logat("Registering PAT. PID: 0x%04X, PacketType::SUPPORTED_SECTION", pat->_program_map_pid);
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
					logat("Registering PMT. PID: 0x%04X, PacketType::SECTION", es_info->_elementary_pid);
					_packet_type_table.emplace(es_info->_elementary_pid, PacketType::SECTION);
				}
				else 
				{
					logat("Registering PMT. PID: 0x%04X, PacketType::PES", es_info->_elementary_pid);
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
			logai("Ignored unsupported or unknown section (table id: %d)", section->TableId());
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

	void MpegTsDepacketizer::LogContinuityBreak()
	{
		_cc_break_count++;

		const int64_t now = ov::Clock::NowMSec();

		if ((_cc_break_last_warn_msec == 0) ||
			((now - _cc_break_last_warn_msec) >= MPEGTS_CC_BREAK_WARN_INTERVAL_MSEC))
		{
			logaw("MPEG-TS continuity break; discarding partial data (%u occurrence(s) since last report)",
				  static_cast<uint32_t>(_cc_break_count));

			_cc_break_last_warn_msec = now;
			_cc_break_count			 = 0;
		}
	}

	void MpegTsDepacketizer::DiscardPesDraft(uint16_t pid)
	{
		std::scoped_lock lock(_pes_draft_map_lock);
		_pes_draft_map.erase(pid);
	}

	void MpegTsDepacketizer::DiscardSectionDraft(uint16_t pid)
	{
		std::scoped_lock lock(_section_draft_map_lock);
		_section_draft_map.erase(pid);
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
			logai("Unsupported PES has been received. (pid : %d stream_id : %d)", pes->PID(), pes->StreamId());
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

				case static_cast<uint8_t>(WellKnownStreamTypes::MPEG1_AUDIO):
				case static_cast<uint8_t>(WellKnownStreamTypes::MPEG2_AUDIO):
					track->SetId(pid);
					track->SetMediaType(cmn::MediaType::Audio);
					track->SetCodecId(cmn::MediaCodecId::Mp2);
					track->SetOriginBitstream(cmn::BitstreamFormat::MP2);
					track->SetTimeBase(1, TIMEBASE);
					break;

				case static_cast<uint8_t>(WellKnownStreamTypes::SCTE35):
					// SCTE-35 StreamType is used to Data Track in OvenMediaEngine.  Data track is created elsewhere for common use, so track is not created here.
					continue;

				default:
					// Doesn't support
					logaw("Unsupported stream_type has been received. (pid : %d stream_type : 0x%02x)", pid, es_info->_stream_type);
					continue;
			}

			_media_tracks.emplace(track->GetId(), track);
		}

		_track_list_completed = true;

		return true;
	}
}  // namespace mpegts
