//==============================================================================
//
//  MPEGTS Packet
//
//  Created by Getroot
//  Copyright (c) 2020 AirenSoft. All rights reserved.
//
//==============================================================================

#include "mpegts_packet.h"
#include <base/ovlibrary/byte_io.h>
#include <base/ovlibrary/memory_utilities.h>

#include "mpegts_section.h"
#include "mpegts_pes.h"

#define OV_LOG_TAG	"MPEGTS_PACKET"

namespace mpegts
{
	Packet::Packet()
	{
		_data = std::make_shared<ov::Data>(MPEGTS_MIN_PACKET_SIZE);
		_buffer = _data->GetWritableDataAs<uint8_t>();
	}

	Packet::Packet(const std::shared_ptr<ov::Data> &data)
	{
		if(data->GetLength() < MPEGTS_MIN_PACKET_SIZE)
		{
			return;
		}

		_data = data;
		_buffer = _data->GetWritableDataAs<uint8_t>();
	}

	Packet::~Packet()
	{

	}

	std::shared_ptr<Packet> Packet::Build(const std::shared_ptr<Section> &section, uint8_t continuity_counter)
	{
		auto packet = std::make_shared<Packet>();

		packet->_sync_byte = 0x47;
		packet->_transport_error_indicator = false;
		packet->_payload_unit_start_indicator = true;
		packet->_transport_priority = false;
		packet->_packet_identifier = section->PID();
		packet->_transport_scrambling_control = 0;
		packet->_adaptation_field_control = 0b01;
		packet->_continuity_counter = continuity_counter;

		ov::ByteStream payload_buffer(188);

		if (packet->_payload_unit_start_indicator)
		{
			payload_buffer.Write8(0x00); // Pointer field
		}
		payload_buffer.Write(section->GetData().GetData(), section->GetData().GetLength()); // Section data

		packet->_payload_data = payload_buffer.GetDataPointer();

		packet->_need_to_update_data = true;
		
		return packet;
	}

	std::vector<std::shared_ptr<Packet>> Packet::Build(const std::shared_ptr<Pes> &pes, bool has_pcr, uint8_t continuity_counter)
	{
		std::vector<std::shared_ptr<Packet>> packets;

		if (pes->GetData() == nullptr)
		{
			return packets;
		}

		auto pes_data = pes->GetData()->GetDataAs<uint8_t>();
		size_t pes_data_length = pes->GetData()->GetLength();
		size_t offset = 0;
		uint8_t packet_count = 0;

		logtt("PES Data Length: %zu", pes_data_length);

		size_t total_payload_size = 0;

		bool first_packet = true;
		while (offset < pes_data_length)
		{
			size_t remaining_pes_bytes = pes_data_length - offset;
			size_t payload_buffer_size = MPEGTS_MIN_PACKET_SIZE - MPEGTS_HEADER_SIZE;
			bool has_adaptation_field = false;
			
			// It means that this is the first packet
			if (has_pcr)
			{
				has_adaptation_field = true;
				payload_buffer_size -= 8; // Adaptation field(2) + PCR(6)
			}
			// No PCR, just first packet
			else if (first_packet)
			{
				has_adaptation_field = true;
				payload_buffer_size -= 2; // Adaptation field(2)
			}
			
			// We always use adaptation field for the last packet 
			// It may be last packet, but if the remaining bytes are 183, it is not the last packet
			if (remaining_pes_bytes < payload_buffer_size)
			{
				// the last packet
				if (has_adaptation_field == false)
				{
					// the last packet needs adaptation field
					has_adaptation_field = true;
					payload_buffer_size -= 2; // Adaptation field
				}
			}

			size_t payload_size = std::min(payload_buffer_size, remaining_pes_bytes);

			logtt("remaining (%d) payload_size(%d) payload_buffer_size(%d) has_adaptation_field(%d)", remaining_pes_bytes, payload_size, payload_buffer_size, has_adaptation_field);

			total_payload_size += payload_size;
			logtt("Payload Size: %d / %d", payload_size, total_payload_size);

			auto packet = std::make_shared<Packet>();

			packet->_sync_byte = 0x47;
			packet->_transport_error_indicator = false;
			packet->_payload_unit_start_indicator = first_packet;
			packet->_transport_priority = false;
			packet->_packet_identifier = pes->PID();
			packet->_transport_scrambling_control = 0;

			// adaptation_field_control
			// 01: No adaptation_field, payload only
			// 10: Adaptation_field only, no payload
			// 11: Adaptation_field followed by payload

			if (has_adaptation_field == false && payload_size > 0)
			{
				packet->_adaptation_field_control = 0b01;
			}
			else if (has_adaptation_field == true && payload_size > 0)
			{
				packet->_adaptation_field_control = 0b11;
			}
			else
			{
				// rare case
				packet->_adaptation_field_control = 0b10;
			}

			packet->_continuity_counter = (continuity_counter + packet_count) % 16;
			packet_count ++;

			if (has_adaptation_field)
			{
				size_t stuffing_bytes = payload_buffer_size - payload_size;
				packet->_adaptation_field._length = 1 + (has_pcr ? 6 : 0) + stuffing_bytes; // flags(8bits) + PCR(6) + stuffing_bytes
				packet->_adaptation_field._pcr_flag = has_pcr;
				packet->_adaptation_field._random_access_indicator = true;

				packet->_adaptation_field_size = 1 + packet->_adaptation_field._length; // length(8bits) + flags(8bits) + PCR(6) + stuffing_bytes

				if (packet->_adaptation_field._pcr_flag)
				{
					// PCR
					uint64_t pcr_base = pes->Pcr() / 300;
					uint32_t pcr_ext = pes->Pcr() % 300;
					// auto most_significant_32_bits_pcr = static_cast<uint32_t>(pcr_base >> 1);
					// auto pcr_last_bit_reserved_and_extension = static_cast<uint16_t>(((pcr_base & 0x1) << 15) | 0x7e00);

					packet->_adaptation_field._pcr._base = pcr_base & 0x1FFFFFFFF; // 33 bits
					packet->_adaptation_field._pcr._reserved = 0x3F; // 6 bits
					packet->_adaptation_field._pcr._extension = pcr_ext & 0x1FF; // 9 bits
				}

				packet->_adaptation_field._stuffing_bytes = stuffing_bytes;
			}

			auto total_packet_size = MPEGTS_HEADER_SIZE + packet->_adaptation_field_size + payload_size;
			logtt("Packet: PID(%d) ContinuityCounter(%d) AdaptationFieldControl(%d) AdaptationFieldSize(%d) PayloadSize(%d) Total(%d)", packet->_packet_identifier, packet->_continuity_counter, packet->_adaptation_field_control, packet->_adaptation_field_size, payload_size, total_packet_size);

			// Payload
			if (payload_size > 0)
			{
				packet->_payload_data = std::make_shared<ov::Data>(pes_data + offset, payload_size);
			}
			else
			{
				logtc("Unexpected payload size: %d", payload_size);
			}

			offset += packet->_payload_data->GetLength();

			packet->_need_to_update_data = true;

			OV_ASSERT(offset <= pes_data_length, "Offset is out of range");

			packets.push_back(packet);

			// just set the pcr to the first packet
			has_pcr = false;
			first_packet = false;
		}

		return packets;
	}

	void Packet::UpdateData()
	{
		// Make data
		auto ts_writer = std::make_shared<ov::BitWriter>(MPEGTS_MIN_PACKET_SIZE, 0xFF);

		// Header
		ts_writer->WriteBytes<uint8_t>(_sync_byte);
		ts_writer->WriteBits(1, _transport_error_indicator);
		ts_writer->WriteBits(1, _payload_unit_start_indicator);
		ts_writer->WriteBits(1, _transport_priority);
		ts_writer->WriteBits(13, _packet_identifier);
		ts_writer->WriteBits(2, _transport_scrambling_control);
		ts_writer->WriteBits(2, _adaptation_field_control);
		ts_writer->WriteBits(4, _continuity_counter);

		bool has_adaptation_field = _adaptation_field_control & 0b10;
		bool has_payload = _adaptation_field_control & 0b01;

		if (has_adaptation_field)
		{
			ts_writer->WriteBytes<uint8_t>(_adaptation_field._length);
			ts_writer->WriteBits(1, _adaptation_field._discontinuity_indicator);
			ts_writer->WriteBits(1, _adaptation_field._random_access_indicator);
			ts_writer->WriteBits(1, _adaptation_field._elementary_stream_priority_indicator);
			ts_writer->WriteBits(1, _adaptation_field._pcr_flag);
			ts_writer->WriteBits(1, _adaptation_field._opcr_flag);
			ts_writer->WriteBits(1, _adaptation_field._splicing_point_flag);
			ts_writer->WriteBits(1, _adaptation_field._transport_private_data_flag);
			ts_writer->WriteBits(1, _adaptation_field._adaptation_field_extension_flag); 

			// PCR
			if (_adaptation_field._pcr_flag)
			{
				ts_writer->WriteBits(33, _adaptation_field._pcr._base);
				ts_writer->WriteBits(6, _adaptation_field._pcr._reserved);
				ts_writer->WriteBits(9, _adaptation_field._pcr._extension);
			}

			// Stuffing bytes
			for (size_t i = 0; i < _adaptation_field._stuffing_bytes; i++)
			{
				ts_writer->WriteBytes<uint8_t>(0xFF);
			}
		}

		size_t payload_offset = ts_writer->GetDataSize();
		if (has_payload)
		{
			// Copy payload
			ts_writer->WriteData(_payload_data->GetDataAs<uint8_t>(), _payload_data->GetLength());
		}

		if (ts_writer->GetDataSize() != MPEGTS_MIN_PACKET_SIZE)
		{
			if (_packet_identifier != 0 && _packet_identifier != 256)
			{
				logtc("mpegts:Packet - PES size is not 188 bytes: %d, pid(%d)", ts_writer->GetDataSize(), _packet_identifier);
			}
		}

		_data = ts_writer->GetDataObject();
		if (_data->GetLength() != MPEGTS_MIN_PACKET_SIZE)
		{
			logti("mpegts:Packet - Packet size is not 188 bytes: %d, pid(%d)", ts_writer->GetDataSize(), _packet_identifier);
		}

		_buffer = _data->GetWritableDataAs<uint8_t>();
		_payload = _buffer + payload_offset;
		_payload_length = _data->GetLength() - payload_offset;

		_need_to_update_data = false;
	}

	std::shared_ptr<const ov::Data> Packet::GetData()
	{
		if (_need_to_update_data)
		{
			UpdateData();
		}

		return _data;
	}

	size_t Packet::GetDataLength()
	{
		return GetData() == nullptr ? 0 : GetData()->GetLength();
	}

	// Getter
	uint8_t Packet::SyncByte()
	{
		return _sync_byte;
	}

	bool Packet::TransportErrorIndicator()
	{
		return _transport_error_indicator;
	}

	bool Packet::PayloadUnitStartIndicator()
	{
		return _payload_unit_start_indicator;
	}

	uint16_t Packet::PacketIdentifier()
	{
		return _packet_identifier;
	}

	uint8_t Packet::TransportScramblingControl()
	{
		return _transport_scrambling_control;
	}

	uint8_t Packet::AdaptationFieldControl()
	{
		return _adaptation_field_control;
	}

	bool Packet::HasAdaptationField()
	{
		// 01: No adaptation_field, payload only
		// 10: Adaptation_field only, no payload
		// 11: Adaptation_field followed by payload
		return OV_GET_BIT(_adaptation_field_control, 1);
	}
	
	bool Packet::HasPayload()
	{
		// 01: No adaptation_field, payload only
		// 10: Adaptation_field only, no payload
		// 11: Adaptation_field followed by payload
		return OV_GET_BIT(_adaptation_field_control, 0);
	}

	uint8_t Packet::ContinuityCounter()
	{
		return _continuity_counter;
	}

	void Packet::SetContinuityCounter(uint8_t continuity_counter)
	{
		_continuity_counter = continuity_counter;

		_need_to_update_data = true;
	}

	const AdaptationField& Packet::GetAdaptationField()
	{
		return _adaptation_field;
	}

	const uint8_t* Packet::Payload()
	{
		return _payload;
	}

	size_t Packet::PayloadLength()
	{
		return _payload_length;
	}

	uint32_t Packet::Parse()
	{
		// already parsed
		if (_ts_parser != nullptr)
		{
			return 0;
		}

		// this time, ome only supports for 188 bytes mpegts packet
		if (_data->GetLength() < MPEGTS_MIN_PACKET_SIZE)
		{
			return 0;
		}

		_ts_parser = std::make_shared<BitReader>(_buffer, _data->GetLength());

		//  76543210  76543210  76543210  76543210
		// [ssssssss][tpTPPPPP][PPPPPPPP][SSaacccc]...

		_sync_byte = _ts_parser->ReadBytes<uint8_t>();
		_transport_error_indicator = _ts_parser->ReadBoolBit();
		if (_transport_error_indicator)
		{
			// error
			return 0;	
		}

		_payload_unit_start_indicator = _ts_parser->ReadBoolBit();
		_transport_priority = _ts_parser->ReadBit();
		_packet_identifier = _ts_parser->ReadBits<uint16_t>(13);
		_transport_scrambling_control = _ts_parser->ReadBits<uint8_t>(2);
		_adaptation_field_control = _ts_parser->ReadBits<uint8_t>(2);
		_continuity_counter = _ts_parser->ReadBits<uint8_t>(4);
		
		if (HasAdaptationField())
		{
			if (ParseAdaptationHeader() == false)
			{
				return 0;
			}

			_adaptation_field_size = 1 + _adaptation_field._length; // length(8bits) + adaptation_field.length
		}

		if (HasPayload())
		{
			if (ParsePayload() == false)
			{
				return 0;
			}
		}
		
		// Now, it must be 188 bytes
		if (_ts_parser->BytesConsumed() != MPEGTS_MIN_PACKET_SIZE)
		{
			logte("Invalid mpegts packet size: %d, consumed: %d", _data->GetLength(), _ts_parser->BytesConsumed());
			return 0;
		}

		return _ts_parser->BytesConsumed();
	}

	bool Packet::ParseAdaptationHeader()
	{
		_adaptation_field._length = _ts_parser->ReadBytes<uint8_t>();
		if (_adaptation_field._length > MPEGTS_MIN_PACKET_SIZE - MPEGTS_HEADER_SIZE)
		{
			logte("Invalid adaptation field length: %d", _adaptation_field._length);
			return false;
		}
		
		_ts_parser->StartSection();

		if(_adaptation_field._length > 0)
		{
			_adaptation_field._discontinuity_indicator = _ts_parser->ReadBoolBit();
			_adaptation_field._random_access_indicator = _ts_parser->ReadBoolBit();
			_adaptation_field._elementary_stream_priority_indicator = _ts_parser->ReadBoolBit();

			// 5 flags
			_adaptation_field._pcr_flag = _ts_parser->ReadBoolBit();
			_adaptation_field._opcr_flag = _ts_parser->ReadBoolBit();
			_adaptation_field._splicing_point_flag = _ts_parser->ReadBoolBit();
			_adaptation_field._transport_private_data_flag = _ts_parser->ReadBoolBit();
			_adaptation_field._adaptation_field_extension_flag = _ts_parser->ReadBoolBit();

			// Need to parse pcr, opcr, splicing_point_flag, _transport_private_data_flag, _adaptation_field_extension_flag
			if(_adaptation_field._pcr_flag == true)
			{
				_adaptation_field._pcr._base = _ts_parser->ReadBits<uint64_t>(33);
				_adaptation_field._pcr._reserved = _ts_parser->ReadBits<uint8_t>(6);
				_adaptation_field._pcr._extension = _ts_parser->ReadBits<uint16_t>(9); 
			}

			if(_adaptation_field._opcr_flag == true)
			{
				// We don't use it now, skip for splicing point flag
				_ts_parser->SkipBytes(6);
			}

			if(_adaptation_field._splicing_point_flag == true)
			{
				_adaptation_field._splice_countdown = _ts_parser->ReadBytes<uint8_t>();
			}

			if(_adaptation_field._transport_private_data_flag)
			{
				// We don't use it now
			}

			if(_adaptation_field._adaptation_field_extension_flag)
			{
				// We don't use it now
			}
		}	
		
		if (_ts_parser->BytesSetionConsumed() > _adaptation_field._length)
		{
			logte("Invalid adaptation field length: %d, consumed: %d", _adaptation_field._length, _ts_parser->BytesSetionConsumed());
			return false;
		}

		// It may contain 
		auto skip_bytes = _adaptation_field._length - _ts_parser->BytesSetionConsumed();

		return _ts_parser->SkipBytes(skip_bytes);
	}

	bool Packet::ParsePayload()
	{
		if (_packet_size < _ts_parser->BytesConsumed())
		{
			logte("Invalid packet size: %d, consumed: %d", _packet_size, _ts_parser->BytesConsumed());
			return false;
		}

		_payload = _ts_parser->CurrentPosition();
		_payload_length = _packet_size - _ts_parser->BytesConsumed();
		_payload_data = std::make_shared<ov::Data>(_payload, _payload_length);
		
		// Just skip A packet
		return _ts_parser->SkipBytes(_payload_length);
	}

	ov::String Packet::ToDebugString() const
	{
		ov::String str;
		
		str.Format("Packet: TEI(%d) PUSI(%d) Priority(%d) PID(%d) TSC(%d) AdaptationFieldControl(%d) ContinuityCounter(%d) AdaptationFieldSize(%d), Payload Size(%d)", 
						_transport_error_indicator, _payload_unit_start_indicator, _transport_priority, 
						_packet_identifier, _transport_scrambling_control, _adaptation_field_control, _continuity_counter, 
						_adaptation_field_size, _payload_length);

		// Adaptation Field
		if(_adaptation_field._length > 0)
		{
			str.AppendFormat("\n\tAdaptation Field: ");
			// Adaptation Field length
			str.AppendFormat("\n\t\tField Length: %d", _adaptation_field._length);

			if(_adaptation_field._pcr_flag)
			{
				str.AppendFormat("\n\t\tPCR: Base(%lld), Reserved(%d), Extension(%d)", _adaptation_field._pcr._base, _adaptation_field._pcr._reserved, _adaptation_field._pcr._extension);
			}
		}

		// Hex
		str.AppendFormat("\n\tHex: ");
		str.Append(ov::ToHexStringWithDelimiter(_data.get(), ' '));

		return str;
	}
}