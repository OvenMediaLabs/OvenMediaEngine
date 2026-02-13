#include "mpegts_pes.h"
#include "mpegts_private.h"

namespace mpegts
{
	Pes::Pes(uint16_t pid)
	{
		_pid = pid;
	}

	Pes::Pes()
	{
		_pid = 0;
	}

	Pes::~Pes()
	{

	}

	std::shared_ptr<Pes> Pes::Build(uint16_t pid, const std::shared_ptr<const MediaTrack> &track, const std::shared_ptr<const MediaPacket> &media_packet)
	{
		if (media_packet->GetData() == nullptr)
		{
			// should not happen
			logte("Media packet data is null");
			return nullptr;
		}

		auto pes = std::make_shared<Pes>(pid);

		// stream id
		if (track->GetMediaType() == cmn::MediaType::Video)
		{
			pes->_stream_id = 0xE0; // + (pid - MIN_ELEMENTARY_PID); // 0xE0 ~ 0xEF
		}
		else if (track->GetMediaType() == cmn::MediaType::Audio)
		{
			pes->_stream_id = 0xC0; // + (pid - MIN_ELEMENTARY_PID); // 0xC0 ~ 0xDF
		}
		else if (track->GetMediaType() == cmn::MediaType::Data)
		{
			// Now only support 65535 length ID3 tag
			pes->_stream_id = 0xBD; // + (pid - MIN_ELEMENTARY_PID); // 0xBD, 0xBF
		}
		else
		{
			logte("Unsupported media type: %d", static_cast<int>(track->GetMediaType()));
			return nullptr;
		}

		// timestamp
		// Timescale of MPEG-2 TS is 90000
		pes->_pts = (static_cast<double>(media_packet->GetPts()) / track->GetTimeBase().GetTimescale() * TIMEBASE_DBL); // + PCR_OFFSET;
		pes->_dts = (static_cast<double>(media_packet->GetDts()) / track->GetTimeBase().GetTimescale() * TIMEBASE_DBL); // + PCR_OFFSET;

		pes->_pcr = (pes->_dts/*- PCR_OFFSET*/) * 300;

		// maximum timestamp is 0x1FFFFFFFF
		pes->_pts = pes->_pts & 0x1FFFFFFFF;
		pes->_dts = pes->_dts & 0x1FFFFFFFF;

		pes->_pes_packet_length = 0; // later it will be updated

		// Optional PES Header
		pes->_marker_bits = 0b10;
		pes->_scrambling_control = 0;
		pes->_priority = 0;
		pes->_data_alignment_indicator = true;
		pes->_copyright = 0;
		pes->_original_or_copy = 0;

		if (track->GetMediaType() == cmn::MediaType::Video)
		{
			pes->_pts_dts_flags = 0b11; // pts and dts
		}
		else
		{
			pes->_pts_dts_flags = 0b10; // only pts
		}
		pes->_escr_flag = 0;
		pes->_es_rate_flag = 0;
		pes->_dsm_trick_mode_flag = 0;
		pes->_additional_copy_info_flag = 0;
		pes->_crc_flag = 0;
		pes->_extension_flag = 0;
		pes->_header_data_length = 0; // later it will be updated

		// Data
		pes->_media_track = track;
		pes->_media_packet = media_packet;

		pes->_need_to_update_data = true;

		return pes;
	}

	bool Pes::UpdateData()
	{
		// Build data
		ov::BitWriter optional_data(16);

		WriteTimestamp(&optional_data, _pts, _dts);
		_header_data_length = optional_data.GetDataSize();
		
		// Optional Header + Optional Data + Payload
		ov::BitWriter optional_header(32);

		// fist, build optional pes header + data to get packet length
		optional_header.WriteBits(2, _marker_bits);
		optional_header.WriteBits(2, _scrambling_control);
		optional_header.WriteBits(1, _priority);
		optional_header.WriteBits(1, _data_alignment_indicator);
		optional_header.WriteBits(1, _copyright);
		optional_header.WriteBits(1, _original_or_copy);
		optional_header.WriteBits(2, _pts_dts_flags);
		optional_header.WriteBits(1, _escr_flag);
		optional_header.WriteBits(1, _es_rate_flag);
		optional_header.WriteBits(1, _dsm_trick_mode_flag);
		optional_header.WriteBits(1, _additional_copy_info_flag);
		optional_header.WriteBits(1, _crc_flag);
		optional_header.WriteBits(1, _extension_flag);
		optional_header.WriteBits(8, _header_data_length);

		// PES Packet Data
		ov::BitWriter pes_packet_data(_media_packet->GetDataLength() + 64);

		// PES Header
		pes_packet_data.WriteBytes<uint8_t>(_start_code_prefix_1);
		pes_packet_data.WriteBytes<uint8_t>(_start_code_prefix_2);
		pes_packet_data.WriteBytes<uint8_t>(_start_code_prefix_3);
		pes_packet_data.WriteBytes<uint8_t>(_stream_id);

		uint64_t pes_packet_length = optional_header.GetDataSize() + optional_data.GetDataSize() + _media_packet->GetDataLength();
		if (pes_packet_length > 0xFF && _media_track->GetMediaType() == cmn::MediaType::Video)
		{
			_pes_packet_length = 0;
		}
		else
		{
			_pes_packet_length = static_cast<uint16_t>(pes_packet_length);
		}

		pes_packet_data.WriteBytes<uint16_t>(_pes_packet_length);

		// Optional Header
		pes_packet_data.WriteData(optional_header.GetData(), optional_header.GetDataSize());

		// Optional Data
		pes_packet_data.WriteData(optional_data.GetData(), optional_data.GetDataSize());

		auto payload_offset = pes_packet_data.GetDataSize();

		// Payload
		pes_packet_data.WriteData(_media_packet->GetData()->GetDataAs<uint8_t>(), _media_packet->GetDataLength());

		_data = pes_packet_data.GetDataObject();
		_payload = _data->GetWritableDataAs<uint8_t>() + payload_offset;
		_payload_length = _data->GetLength() - payload_offset;

		_need_to_update_data = false;

		return true;
	}

	// return consumed length
	size_t Pes::AppendData(const uint8_t *data, uint32_t length)
	{
		if (_data == nullptr)
		{
			_data = std::make_shared<ov::Data>();
		}

		// Prevents _data from becoming too large due to malicious attacks or client bugs.
		if (_data->GetLength() > MPEGTS_MAX_PES_PACKET_SIZE)
		{
			logtc("Could not append data anymore. PES packet size is too large: %zu. It may be a malicious attack or client bug.", _data->GetLength());
			return 0;
		}

		// it indicates how many bytes are used in this function
		// it means current position in data and remains length - consumed_length
		size_t consumed_length = 0;

		if(_pes_header_parsed == false)
		{
			// Parsing header first
			auto current_length = _data->GetLength();
			auto need_length = static_cast<size_t>(MPEGTS_PES_HEADER_SIZE) - current_length;

			auto append_length = std::min(need_length, static_cast<size_t>(length - consumed_length));
			_data->Append(data + consumed_length, append_length);
			consumed_length += append_length;

			if(_data->GetLength() >= MPEGTS_PES_HEADER_SIZE)
			{
				BitReader parser(_data->GetWritableDataAs<uint8_t>(), MPEGTS_PES_HEADER_SIZE);
				if(ParsePesHeader(&parser) == false)
				{
					logte("Could not parse table header");
					return 0;
				}
			}
		}

		if(_pes_optional_header_parsed == false && HasOptionalHeader())
		{
			// Parsing header first
			auto current_length = _data->GetLength();
			// needed data length for parsing pes optional header (9 - current length)
			// need_length cannot be minus value
			auto need_length = static_cast<size_t>(MPEGTS_PES_HEADER_SIZE + MPEGTS_MIN_PES_OPTIONAL_HEADER_SIZE) - current_length;

			auto append_length = std::min(need_length, static_cast<size_t>(length - consumed_length));
			_data->Append(data + consumed_length, append_length);
			consumed_length += append_length;

			if(_data->GetLength() >= MPEGTS_PES_HEADER_SIZE + MPEGTS_MIN_PES_OPTIONAL_HEADER_SIZE)
			{
				BitReader parser(_data->GetWritableDataAs<uint8_t>(), MPEGTS_PES_HEADER_SIZE + MPEGTS_MIN_PES_OPTIONAL_HEADER_SIZE);
				// PES header is already parsed so skips header
				parser.SkipBytes(MPEGTS_PES_HEADER_SIZE);
				if(ParsePesOptionalHeader(&parser) == false)
				{
					logte("Could not parse table header");
					return 0;
				}
			}
		}

		if(_pes_optional_data_parsed == false && HasOptionalData())
		{
			// Parsing header first
			auto current_length = _data->GetLength();
			// needed data length for parsing pes optional header (9 + _header_data_length - current length)
			// need_length cannot be minus value
			auto need_length = static_cast<size_t>(MPEGTS_PES_HEADER_SIZE + MPEGTS_MIN_PES_OPTIONAL_HEADER_SIZE + _header_data_length) - current_length;

			auto append_length = std::min(need_length, static_cast<size_t>(length - consumed_length));
			_data->Append(data + consumed_length, append_length);
			consumed_length += append_length;

			if(_data->GetLength() >= MPEGTS_PES_HEADER_SIZE + MPEGTS_MIN_PES_OPTIONAL_HEADER_SIZE + _header_data_length)
			{
				BitReader parser(_data->GetWritableDataAs<uint8_t>(), MPEGTS_PES_HEADER_SIZE + MPEGTS_MIN_PES_OPTIONAL_HEADER_SIZE + _header_data_length);
				// PES header and optional header are already parsed so skips that
				parser.SkipBytes(MPEGTS_PES_HEADER_SIZE + MPEGTS_MIN_PES_OPTIONAL_HEADER_SIZE);
				if(ParsePesOPtionalData(&parser) == false)
				{
					logte("Could not parse table header");
					return 0;
				}
			}
		}

		// all inputted data is consumed, wait for next packet
		if(consumed_length == length)
		{
			return length;
		}
		
		uint16_t remained_packet_length = 0;
		uint16_t copy_length = length - consumed_length;
		if(_pes_packet_length != 0)
		{
			// How many bytes remains to complete this pes packet
			remained_packet_length = _pes_packet_length - (_data->GetLength() - MPEGTS_PES_HEADER_SIZE);
			copy_length = std::min(copy_length, remained_packet_length);
		}
		
		// If all header and optional data are parsed, all remaining data is payload
		_data->Append(data + consumed_length, copy_length);
		consumed_length += copy_length;

		// Completed
		if(_pes_packet_length != 0 && _pes_packet_length == _data->GetLength() - MPEGTS_PES_HEADER_SIZE)
		{
			SetEndOfData();
		}

		return consumed_length;
	}

	bool Pes::ParsePesHeader(BitReader *parser)
	{
		_start_code_prefix_1 = parser->ReadBytes<uint8_t>();
		_start_code_prefix_2 = parser->ReadBytes<uint8_t>();
		_start_code_prefix_3 = parser->ReadBytes<uint8_t>();
		if(_start_code_prefix_1 != 0x00 || _start_code_prefix_2 != 0x00 || _start_code_prefix_3 != 0x01)
		{	
			logtw("Could not parse pes header. (pid: %d)", _pid);
			return false;
		}

		_stream_id = parser->ReadBytes<uint8_t>();
		_pes_packet_length = parser->ReadBytes<uint16_t>();

		_pes_header_parsed = true;
		return true;
	}

	bool Pes::ParsePesOptionalHeader(BitReader *parser)
	{
		_marker_bits = parser->ReadBits<uint8_t>(2);
		_scrambling_control = parser->ReadBits<uint8_t>(2);
		_priority = parser->ReadBit();
		_data_alignment_indicator = parser->ReadBit();
		_copyright = parser->ReadBit();
		_original_or_copy = parser->ReadBit();
		_pts_dts_flags = parser->ReadBits<uint8_t>(2);
		_escr_flag = parser->ReadBit();
		_es_rate_flag = parser->ReadBit();
		_dsm_trick_mode_flag = parser->ReadBit();
		_additional_copy_info_flag = parser->ReadBit();
		_crc_flag = parser->ReadBit();
		_extension_flag = parser->ReadBit();
		_header_data_length = parser->ReadBytes<uint8_t>();

		_pes_optional_header_parsed = true;
		return true;
	}

	bool Pes::ParsePesOPtionalData(BitReader *parser)
	{
		parser->StartSection();

		bool pts_flag = OV_GET_BIT(_pts_dts_flags, 1);
		bool dts_flag = OV_GET_BIT(_pts_dts_flags, 0);

		// only pts
		if(pts_flag == 1 && dts_flag == 0)
		{
			if(ParseTimestamp(parser, 0b0010, _pts) == false)
			{
				logte("Could not parse PTS");
				return false;
			}

			_dts = _pts;
		}
		// pts, dts
		else if(pts_flag == 1 && dts_flag == 1)
		{
			if ((ParseTimestamp(parser, 0b0011, _pts) == false) ||
				(ParseTimestamp(parser, 0b0001, _dts) == false))
			{
				logte("Could not parse PTS & DTS");
				return false;
			}
		}
		// 01: forbidden, 00: no pts, dts
		else
		{
			logte("Could not parse PTS & DTS");
			return false;
		}

		auto parsed_optional_fields_length = parser->BytesSetionConsumed();
		
		// Another options are not used now, so they are skipped
		parser->SkipBytes(_header_data_length - parsed_optional_fields_length);

		_pes_optional_data_parsed = true;
		return true;
	}

	bool Pes::ParseTimestamp(BitReader *parser, uint8_t start_bits, int64_t &timestamp)
	{
		//  76543210  76543210  76543210  76543210  76543210
		// [ssssTTTm][TTTTTTTT][TTTTTTTm][TTTTTTTT][TTTTTTTm]
		//
		//      TTT   TTTTTTTT  TTTTTTT   TTTTTTTT  TTTTTTT
		//      210   98765432  1098765   43210987  6543210
		//        3              2            1           0
		// s: start_bits
		// m: marker_bit (should be 1)
		// T: timestamp
		auto prefix = parser->ReadBits<uint8_t>(4);
		if (prefix != start_bits)
		{
			logte("Invalid PTS_DTS start bits: %d (%02X), expected: %d (%02X)", prefix, prefix, start_bits, start_bits);
			return false;
		}

		int64_t ts = parser->ReadBits<int64_t>(3) << 30;

		// Check the first marker bit of the first byte
		if (parser->ReadBit() != 0b1)
		{
			logte("Invalid marker bit of the first byte");
			return false;
		}

		ts |= parser->ReadBits<int64_t>(8) << 22;
		ts |= parser->ReadBits<int64_t>(7) << 15;

		// Check the second marker bit of the third byte
		if (parser->ReadBit() != 0b1)
		{
			logte("Invalid marker bit of the third byte");
			return false;
		}

		ts |= parser->ReadBits<int64_t>(8) << 7;
		ts |= parser->ReadBits<int64_t>(7);

		// Check the third marker bit of the fifth byte
		if (parser->ReadBit() != 0b1)
		{
			logte("Invalid marker bit of the third byte");
			return false;
		}

		timestamp = ts;

		return true;
	}

	// we write both pts and dts
	bool Pes::WriteTimestamp(ov::BitWriter *writer, int64_t& pts, int64_t& dts)
	{
		//  76543210  76543210  76543210  76543210  76543210
		// [ssssTTTm][TTTTTTTT][TTTTTTTm][TTTTTTTT][TTTTTTTm]
		//
		//      TTT   TTTTTTTT  TTTTTTT   TTTTTTTT  TTTTTTT
		//      210   98765432  1098765   43210987  6543210
		//        3              2            1           0
		// s: start_bits
		// m: marker_bit (should be 1)
		// T: timestamp
		
		// maximum timestamp is 0x1FFFFFFFF
		// PTS start bits (0010), DTS start bits (0001)

		if (_pts_dts_flags == 0b00 || _pts_dts_flags == 0b01)
		{
			// forbidden
			return false;
		}

		bool pts_flag = OV_GET_BIT(_pts_dts_flags, 1);
		bool dts_flag = OV_GET_BIT(_pts_dts_flags, 0);
		
		// Write PTS
		if (pts_flag == true && dts_flag == true)
		{
			writer->WriteBits(4, 0b0011);
		}
		else if (pts_flag == true && dts_flag == false)
		{
			writer->WriteBits(4, 0b0010);
		}

		writer->WriteBits(3, pts >> 30);
		writer->WriteBits(1, 0b1);
		writer->WriteBits(8, (pts >> 22) & 0xFF);
		writer->WriteBits(7, (pts >> 15) & 0x7F);
		writer->WriteBits(1, 0b1);
		writer->WriteBits(8, (pts >> 7) & 0xFF);
		writer->WriteBits(7, pts & 0x7F);
		writer->WriteBits(1, 0b1);

		if (dts_flag == true)
		{
			writer->WriteBits(4, 0b0001);
			writer->WriteBits(3, dts >> 30);
			writer->WriteBits(1, 0b1);
			writer->WriteBits(8, (dts >> 22) & 0xFF);
			writer->WriteBits(7, (dts >> 15) & 0x7F);
			writer->WriteBits(1, 0b1);
			writer->WriteBits(8, (dts >> 7) & 0xFF);
			writer->WriteBits(7, dts & 0x7F);
			writer->WriteBits(1, 0b1);
		}

		return true;
	}


	// All pes data has been inserted
	bool Pes::SetEndOfData()
	{
		// Set payload
		_payload = _data->GetWritableDataAs<uint8_t>();
		_payload_length = _data->GetLength();

		_payload += MPEGTS_PES_HEADER_SIZE;
		_payload_length -= MPEGTS_PES_HEADER_SIZE;

		if(IsAudioStream() || IsVideoStream())
		{
			_payload += MPEGTS_MIN_PES_OPTIONAL_HEADER_SIZE + _header_data_length;
			_payload_length -= MPEGTS_MIN_PES_OPTIONAL_HEADER_SIZE + _header_data_length;
		}

		_completed = true;
		return true;
	}

	bool Pes::HasOptionalHeader() const
	{
		return _pes_header_parsed && (IsAudioStream() || IsVideoStream());
	}

	bool Pes::HasOptionalData() const
	{
		return HasOptionalHeader() && _pes_optional_header_parsed && (_header_data_length > 0);
	}

	// return true when section is completed
	bool Pes::IsCompleted()
	{
		return _completed;
	}

	// Getter
	std::shared_ptr<const ov::Data> Pes::GetData()
	{
		if (_need_to_update_data == true)
		{
			UpdateData();
		}

		return _data;
	}

	uint16_t Pes::PID() const
	{
		return _pid;
	}

	uint8_t Pes::StreamId() const
	{
		return _stream_id;
	}

	uint16_t Pes::PesPacketLength() const
	{
		return _pes_packet_length;
	}

	uint8_t Pes::ScramblingControl() const
	{
		return _scrambling_control;
	}

	uint8_t Pes::Priority() const
	{
		return _priority;
	}
	
	uint8_t Pes::DataAlignmentIndicator() const
	{
		return _data_alignment_indicator;
	}
	uint8_t Pes::Copyright() const
	{
		return _copyright;
	}
	
	uint8_t Pes::OriginalOrCopy() const
	{
		return _original_or_copy;
	}
	
	int64_t Pes::Pts() const
	{
		return _pts;
	}
	
	int64_t Pes::Dts() const
	{
		return _dts;
	}

	int64_t Pes::Pcr() const
	{
		return _pcr;
	}
	
	const uint8_t* Pes::Payload()
	{
		if (_need_to_update_data == true)
		{
			UpdateData();
		}

		return _payload;
	}
	
	uint32_t Pes::PayloadLength()
	{
		if (_need_to_update_data == true)
		{
			UpdateData();
		}

		return _payload_length;
	}
}