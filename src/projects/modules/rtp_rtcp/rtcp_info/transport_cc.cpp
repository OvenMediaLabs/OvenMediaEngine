#include "transport_cc.h"
#include "rtcp_private.h"
#include <bitset>
#include <base/ovlibrary/byte_io.h>

bool TransportCc::Parse(const RtcpPacket &packet)
{
	const uint8_t *payload = packet.GetPayload();
	size_t payload_size = packet.GetPayloadSize();

	if(payload_size < static_cast<size_t>(MIN_TRANSPORT_CC_RTCP_SIZE))
	{
		logtt("Payload is too small to parse transport-cc");
		return false;
	}

	// Read Common Feedback
	size_t offset = 0; 
	SetSenderSsrc(ByteReader<uint32_t>::ReadBigEndian(&payload[offset]));
	offset += 4;
	SetMediaSsrc(ByteReader<uint32_t>::ReadBigEndian(&payload[offset]));
	offset += 4;
	
	// Read transport-cc feedback
	_base_sequence_number = ByteReader<uint16_t>::ReadBigEndian(&payload[offset]);
	offset += 2;
	_packet_status_count = ByteReader<uint16_t>::ReadBigEndian(&payload[offset]);
	offset += 2;
	_reference_time = ByteReader<uint24_t>::ReadBigEndian(&payload[offset]);
	offset += 3;
	_fb_packet_count = ByteReader<uint8_t>::ReadBigEndian(&payload[offset]);
	offset += 1;

	logtt("Transport-cc: base_sequence_number(%d), packet_status_count(%d), reference_time(%d), fb_sequence_number(%d)", _base_sequence_number, _packet_status_count, _reference_time, _fb_packet_count);

	for (uint16_t i=0; i<_packet_status_count; i++)
	{
		if (offset + PACKET_CHUNK_BYTES > payload_size)
		{
			logtt("Payload is too small to parse transport-cc");
			return false;
		}

		// Read Packet Chunk
		uint16_t packet_chunk = ByteReader<uint16_t>::ReadBigEndian(&payload[offset]);	
		offset += 2;

		// Parse Packet Chunk

		// Run Length Chunk
		//  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5
		// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
		// |0| S |       Run Length        |
		// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
		if ((packet_chunk & 0x8000) == 0)
		{
			// Get Symbol (second/third bits)
			uint8_t symbol = (packet_chunk >> 13) & 0x03;
			// Get Run Length (last 13 bits)
			uint16_t run_length = packet_chunk & 0x1FFF;

			uint8_t delta_size = GetDeltaSize(symbol);

			for (uint16_t j=0; j<run_length; j++)
			{
				auto info = std::make_shared<PacketFeedbackInfo>();
				info->_wide_sequence_number = _base_sequence_number + i + j;
				info->_received = (delta_size > 0);
				info->_delta_size = delta_size;

				_packet_feedbacks.push_back(info); 
			}

			i += run_length - 1;
		}
		//  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5
		// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
		// |1|0|       symbol list         |
		// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
		// One bit in the symbol list indicates one packet, 
		// 0: not received 1: received, small delta
		else if ((packet_chunk & 0x4000) == 0)
		{
			// loop each bits in symbol list, j cannot exceed packet_status_count - i
			uint16_t j=0;
			for (j=0; j<14; j++)
			{
				if (i + j >= _packet_status_count)
				{
					break;
				}

				bool received = ((packet_chunk >> (14 - 1 - j)) & 0x01) == 1;
				uint8_t symbol = received == true ? 1 : 0;
				uint8_t delta_size = GetDeltaSize(symbol);

				auto info = std::make_shared<PacketFeedbackInfo>();
				info->_wide_sequence_number = _base_sequence_number + i + j;
				info->_received = received;
				info->_delta_size = delta_size;
				_packet_feedbacks.push_back(info);
			}

			i += j;
		}
		//  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5
		// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
		// |1|1|       symbol list         |
		// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
		// Two bits in the symbol list indicates one packet, 
		else
		{
			// 2bits
			uint16_t j=0;
			for (j=0; j<7; j++)
			{
				if (i + j >= _packet_status_count)
				{
					break;
				}

				uint8_t symbol = (packet_chunk >> (2 * (7 - 1 - j))) & 0x03;
				uint8_t delta_size = GetDeltaSize(symbol);

				auto info = std::make_shared<PacketFeedbackInfo>();
				info->_wide_sequence_number = _base_sequence_number + i + j;
				info->_received = (delta_size > 0);
				info->_delta_size = delta_size;

				_packet_feedbacks.push_back(info); 
			}

			i += j;
		}
	}

	// Fill received delta info for packet feedbacks
	for (auto& info : _packet_feedbacks)
	{
		int32_t received_delta = 0;
		if (info->_delta_size == 1)
		{
			if (payload_size < static_cast<size_t>(offset + 1))
			{
				logtt("Payload is too small to parse transport-cc");
				return false;
			}

			received_delta = ByteReader<uint8_t>::ReadBigEndian(&payload[offset]);
			offset += 1;
		}
		else if (info->_delta_size == 2)
		{
			if (payload_size < static_cast<size_t>(offset + 2))
			{
				logtt("Payload is too small to parse transport-cc");
				return false;
			}

			auto temp = ByteReader<uint16_t>::ReadBigEndian(&payload[offset]);

			// 0 ~ 0xFFFF to -0x8000 ~ 0x7FFF
			// 0x8000 is the middle point
			received_delta = static_cast<int16_t>(temp);

			offset += 2;
		}
		else 
		{
			// nothing to do
		}

		info->_received_delta = received_delta;
	}

	if (offset != payload_size)
	{
		logte("Even though parsing transport-cc was completed, the payload is not fully parsed");
		return false;
	}

	return true;
}

// RtcpInfo must provide raw data
std::shared_ptr<ov::Data> TransportCc::GetData() const 
{
	ov::ByteStream write_stream(4096);

	// SSRC of packet sender
	write_stream.WriteBE32(_sender_ssrc);
	// SSRC of media source
	write_stream.WriteBE32(_media_ssrc);
	// base sequence number
	write_stream.WriteBE16(_base_sequence_number);
	// packet status count
	write_stream.WriteBE16(_packet_status_count);
	// reference time
	write_stream.WriteBE24(_reference_time);
	// fb pkt count
	write_stream.WriteBE(_fb_packet_count);

	logtt("Feedback - sender_ssrc(%u) media_ssrc(%u) base_sequence_number(%u) packet_status_count(%u) reference_time(%u) fb_sequence_number(%u)", 
		_sender_ssrc, _media_ssrc, _base_sequence_number, _packet_status_count, _reference_time, _fb_packet_count);

	// Make packet status chunk
	uint32_t index = 0;
	while (index < _packet_status_count)
	{
		uint16_t run_length = 0;
		uint16_t one_bit_symbol_length = 0;

		uint16_t symbol_count = 0;
		uint8_t chunk_type = 0; // 0 : run length chunk, 1 : one bit symbol chunk, 2 : two bit symbol chunk

		CountSymbolContinuity(index, run_length, one_bit_symbol_length);

		logtt("index(%u) run_length(%u) one_bit_symbol_length(%u)", index, run_length, one_bit_symbol_length);

		if (run_length > 14)
		{
			// Make run length chunk
			symbol_count = run_length;
			// max run length is 13 bits (0x1FFF)
			if (symbol_count > 0x1FFF)
			{
				symbol_count = 0x1FFF;
			}

			chunk_type = 0;
		}
		else if (one_bit_symbol_length >= 14)
		{
			// Make one bit symbol chunk
			symbol_count = 14;
			chunk_type = 1;
		}
		else if (run_length > 7)
		{
			// Make run length chunk
			symbol_count = run_length;
			chunk_type = 0;
		}
		else
		{
			// Make two bit symbol chunk
			symbol_count = std::min(7, static_cast<int>(_packet_status_count - index));
			chunk_type = 2;
		}

		// Make chunk header
		uint16_t packet_status_chunk = 0;
		if (chunk_type == 0)
		{
			// Run length chunk
			//  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5
			// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
			// |0| S |     run length          |
			// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
			auto sample = _packet_feedbacks[index];
			uint8_t symbol = sample->_delta_size;

			// 1 bit 0
			packet_status_chunk |= 0x0000;
			
			// 2 bits symbol
			packet_status_chunk |= (symbol << 13);

			// 13 bits run length
			packet_status_chunk |= (run_length & 0x1FFF);
		}
		else if (chunk_type == 1)
		{
			// One bit symbol chunk
			//  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5
			// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
			// |1|0|       symbol list         |
			// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
			// One bits in the symbol list indicates one packet, 

			// write 1 0
			packet_status_chunk |= 0x8000;

			for (auto i = 0; i < symbol_count; i++)
			{
				auto sample = _packet_feedbacks[index + i];
				uint16_t symbol = sample->_delta_size;

				// write 1 bit symbol (0 or 1)
				packet_status_chunk |= (symbol & 0x01) << (13 - i);
			}
		}
		else if (chunk_type == 2)
		{
			// Two bit symbol chunk
			//  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5
			// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
			// |1|1|       symbol list         |
			// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
			// Two bits in the symbol list indicates one packet, 

			// write 1 1
			packet_status_chunk |= 0xC000;

			for (auto i = 0; i < symbol_count; i++)
			{
				auto sample = _packet_feedbacks[index + i];
				uint16_t symbol = sample->_delta_size;

				// write 2bit symbol
				packet_status_chunk |= (symbol & 0x03) << (12 - (i * 2));
			}
		}

		// write chunk to buffer
		write_stream.WriteBE16(packet_status_chunk);

		// print packet_status_chunk as binary
		logtt("[%s]", std::bitset<16>(packet_status_chunk).to_string().c_str());

		index += symbol_count;
	}

	// write delta
	logtt("Feedback packet status count (%u) reference_time(%u ms)", _packet_feedbacks.size(), _reference_time * 64);
	for (const auto &info : _packet_feedbacks)
	{
		logtt("Feedback - seq(%u) delta_size(%u) received_delta (%d ms)", info->_wide_sequence_number, info->_delta_size, (info->_received_delta * 250) / 1000);

		if (info->_delta_size == 1)
		{
			write_stream.WriteBE(static_cast<uint8_t>(info->_received_delta));
		}
		else if (info->_delta_size == 2)
		{
			write_stream.WriteBE16(static_cast<int16_t>(info->_received_delta));
		}
	}

	// RTCP Payload length must be 4 bytes aligned
	size_t length = write_stream.GetLength();
	size_t padding = (4 - (length % 4)) % 4;
	for (size_t i = 0; i < padding; i++)
	{
		write_stream.WriteBE(static_cast<uint8_t>(0));
		_has_padding = true;
	}

	logtt("Feedback - packet_status_count(%u/%u) reference_time(%u)", _packet_feedbacks.size(), _packet_feedbacks.size(), _reference_time);

	return write_stream.GetDataPointer();
}

bool TransportCc::AddPacketFeedbackInfo(const std::shared_ptr<PacketFeedbackInfo> &packet_feedback_info)
{
	auto wide_sequence_number = packet_feedback_info->_wide_sequence_number;
	
	// Calc diff between _next_sequence_number and wide_sequence_number
	// Normally, diff is 0 (waiting for packet that has next_sequence_number)
	uint16_t diff = wide_sequence_number >= _next_sequence_number ? 
							wide_sequence_number - _next_sequence_number : 
							0xFFFF - _next_sequence_number + wide_sequence_number + 1;
	if (diff > 0x1000)
	{
		// It may be a old packet
		logtw("AddPacketFeedbackInfo - seq(%u) is too old", wide_sequence_number);
		return false;
	}
	
	// Add missing packet
	for (auto i = 0; i < diff; i++)
	{
		logtw("AddPacketFeedbackInfo - Add missing packet - seq(%u)", _next_sequence_number);
		_packet_feedbacks.push_back(std::make_shared<PacketFeedbackInfo>(_next_sequence_number, false));
		_packet_status_count++;
		_next_sequence_number++;
	}

	logtt("AddPacketFeedbackInfo - base seq(%u) seq(%u) received(%s) delta(%d) delta_size(%d)", 
		_base_sequence_number, packet_feedback_info->_wide_sequence_number, packet_feedback_info->_received?"true":"false",
		packet_feedback_info->_received_delta, packet_feedback_info->_delta_size);

	_packet_feedbacks.push_back(packet_feedback_info);
	_packet_status_count ++;
	_next_sequence_number ++;

	return true;
}

// feedbacks_start_index : Starting position of _packet_feedbacks
// run_length : The number of times the same symbol length is consecutive
// one_bit_symbol_length : Number of consecutive symbol lengths of 0 or 1
bool TransportCc::CountSymbolContinuity(uint32_t feedbacks_start_index, uint16_t &run_length, uint16_t &one_bit_symbol_length) const
{
	if (feedbacks_start_index >= _packet_feedbacks.size())
	{
		return false;
	}

	uint8_t first_symbol_length = _packet_feedbacks[feedbacks_start_index]->_delta_size;
	bool is_run_length_continuity = true;
	bool is_one_bit_symbol_continuity = false;

	run_length = 1;

	one_bit_symbol_length = 0;
	if (first_symbol_length == 0 || first_symbol_length == 1)
	{
		is_one_bit_symbol_continuity = true;
		one_bit_symbol_length = 1;
	}

	for (auto i = feedbacks_start_index + 1; i < _packet_feedbacks.size(); i++)
	{
		auto info = _packet_feedbacks[i];
		
		// Check run length
		if (is_run_length_continuity && info->_delta_size == first_symbol_length)
		{
			run_length++;
		}
		else
		{
			is_run_length_continuity = false;
		}

		// Check one bit symbol length
		if (is_one_bit_symbol_continuity && (info->_delta_size == 0 ||  info->_delta_size == 1))
		{
			one_bit_symbol_length++;
		}
		else
		{
			is_one_bit_symbol_continuity = false;
		}

		// max run length : 13bits max value is 0x1FFF
		// max one bit symbol length : 14
		if (run_length >= 0x1FFF)
		{
			break;
		}
		else if (is_run_length_continuity == false && one_bit_symbol_length >= 14)
		{
			break;
		}
		else if (!is_run_length_continuity && !is_one_bit_symbol_continuity)
		{
			break;
		}
	}

	return true;
}

void TransportCc::DebugPrint()
{
	for (auto& info : _packet_feedbacks)
	{
		logtt("PacketFeedbackInfo: reference_time=%u, fb_seq_no=%d, wide_sequence_number=%d, received=%d, delta_size=%d, received_delta=%d", 
		_reference_time, _fb_packet_count, info->_wide_sequence_number, info->_received, info->_delta_size, info->_received_delta);
	}
}