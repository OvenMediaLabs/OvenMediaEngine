//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2025 AirenSoft. All rights reserved.
//
//==============================================================================
#include "rtmp_chunk_writer.h"

#include "../rtmp_private.h"
#include "./rtmp_utilities.h"

namespace modules::rtmp
{
	ChunkWriter::ChunkWriter(size_t chunk_size)
		: _chunk_size(chunk_size)
	{
	}

	// Calculate the length of the data to be sent
	// <Block #x> are divied from the payload in units of _chunk_size
	//
	// The length will be calculated based on the following form:
	//
	//   <Chunk header> + <Block #1> + <Type3 header> + <Block #2> + ... + <Type3 header> + <Block #n>
	size_t ChunkWriter::CalculateDataLength(const std::shared_ptr<const ChunkHeader> &chunk_header, size_t payload_length) const
	{
		if (chunk_header == nullptr)
		{
			OV_ASSERT(false, "Chunk header is null");
			logte("Could not calculate length - chunk header is null");
			return 0;
		}

		if (_chunk_size == 0)
		{
			OV_ASSERT(false, "Chunk size is zero");
			logte("Could not calculate length - chunk size is zero");
			return 0;
		}

		auto data_length = GetChunkHeaderLength(
							   chunk_header->basic_header.chunk_stream_id,
							   chunk_header->basic_header.format_type,
							   chunk_header->is_extended_timestamp) +
						   payload_length;

		if (payload_length > _chunk_size)
		{
			payload_length -= _chunk_size;

			const auto type3_header_length = GetChunkHeaderLength(
				chunk_header->basic_header.chunk_stream_id,
				MessageHeaderType::T3,
				chunk_header->is_extended_timestamp);

			// Calculate how many blocks are needed for the remaining payload
			const auto payload_block_count = ((payload_length - 1) / _chunk_size) + 1;

			data_length += (payload_block_count * type3_header_length);
		}

		return data_length;
	}

	void ChunkWriter::SerializeBasicHeader(MessageHeaderType format_type, uint32_t chunk_stream_id, ov::ByteStream &byte_stream) const
	{
		auto first_byte = (ov::ToUnderlyingType(format_type) << 6) & 0xc0;

		switch (chunk_stream_id)
		{
			case 0b000000:
				// basic_header_length == 2

				// Value 0 indicates the 2 byte form and an ID in the range of 64-319
				// (the second byte + 64)

				byte_stream.Write8(first_byte | 0x00);
				byte_stream.Write8((chunk_stream_id - 64) & 0xff);
				break;

			case 0b000001:
				// basic_header_length == 3

				// Value 1 indicates the 3 byte form and an ID in the range of 64-65599
				// ((the third byte) * 256 + the second byte + 64)

				byte_stream.Write8(first_byte | 0x01);
				chunk_stream_id -= 64;
				byte_stream.Write8(chunk_stream_id & 0xff);
				chunk_stream_id >>= 8;
				byte_stream.Write8(chunk_stream_id & 0xff);
				break;

			default:
				// basic_header_length == 1

				// Chunk stream IDs 2-63 can be encoded in the 1-byte version of this field
				byte_stream.Write8(first_byte | (chunk_stream_id & 0b00111111));

				break;
		}
	}

	void ChunkWriter::SerializeMessageHeader(const std::shared_ptr<const ChunkHeader> &chunk_header, ov::ByteStream &byte_stream) const
	{
		auto format_type = chunk_header->basic_header.format_type;

		switch (format_type)
		{
			case MessageHeaderType::T0:
				// timestamp
				if (chunk_header->is_extended_timestamp)
				{
					byte_stream.WriteBE24(EXTENDED_TIMESTAMP_VALUE);
				}
				else
				{
					byte_stream.WriteBE24(chunk_header->message_header.type_0.timestamp);
				}

				// length
				byte_stream.WriteBE24(chunk_header->message_header.type_0.length);

				// type_id
				byte_stream.Write8(ov::ToUnderlyingType(chunk_header->message_header.type_0.type_id));

				// stream_id
				byte_stream.WriteLE32(chunk_header->message_header.type_0.stream_id);

				// extended timestamp
				if (chunk_header->is_extended_timestamp)
				{
					byte_stream.WriteBE32(chunk_header->extended_timestamp);
				}

				break;

			case MessageHeaderType::T1:
				// timestamp delta
				if (chunk_header->is_extended_timestamp)
				{
					byte_stream.WriteBE24(EXTENDED_TIMESTAMP_VALUE);
				}
				else
				{
					byte_stream.WriteBE24(chunk_header->message_header.type_1.timestamp_delta);
				}

				// length
				byte_stream.WriteBE24(chunk_header->message_header.type_1.length);

				// type_id
				byte_stream.Write8(ov::ToUnderlyingType(chunk_header->message_header.type_1.type_id));

				// extended timestamp
				if (chunk_header->is_extended_timestamp)
				{
					byte_stream.WriteBE32(chunk_header->message_header.type_1.timestamp_delta);
				}

				break;

			case MessageHeaderType::T2:
				// timestamp delta
				if (chunk_header->is_extended_timestamp)
				{
					byte_stream.WriteBE24(EXTENDED_TIMESTAMP_VALUE);
				}
				else
				{
					byte_stream.WriteBE24(chunk_header->message_header.type_2.timestamp_delta);
				}

				// extended timestamp
				if (chunk_header->is_extended_timestamp)
				{
					byte_stream.WriteBE32(chunk_header->message_header.type_2.timestamp_delta);
				}

				break;

			case MessageHeaderType::T3:
				break;
		}
	}

	void ChunkWriter::Serialize(const std::shared_ptr<const ChunkWriteInfo> &write_info,
								const std::shared_ptr<const ChunkHeader> &chunk_header,
								ov::ByteStream &byte_stream) const
	{
		if (_chunk_size == 0)
		{
			OV_ASSERT(false, "Chunk size is zero");
			logte("Could not serialize payload - chunk size is zero");
			return;
		}

		if (chunk_header == nullptr)
		{
			return;
		}

		auto payload = write_info->GetPayload();
		auto payload_length = write_info->GetPayloadLength();

		if ((payload == nullptr) || (payload_length == 0))
		{
			return;
		}

		// Write basic header for `chunk_header`
		SerializeBasicHeader(chunk_header, byte_stream);
		SerializeMessageHeader(chunk_header, byte_stream);

		// Create type3 header for the payload
		// (type3 doesn't have a message header)
		ov::ByteStream type3_header_stream(MAX_TYPE3_PACKET_HEADER_LENGTH);
		SerializeBasicHeader(MessageHeaderType::T3, chunk_header->basic_header.chunk_stream_id, type3_header_stream);

		const auto type3_header = type3_header_stream.GetData();
		auto remaining_payload_length = payload_length;
		auto current_position = reinterpret_cast<const uint8_t *>(payload);

		// Since the header for the first payload block was already written above,
		// only the payload is written at the beginning, and from the second block, a type 3 header is written.
		bool need_to_write_type3_header = false;

		while (remaining_payload_length > 0)
		{
			if (need_to_write_type3_header)
			{
				// Write type 3 header
				byte_stream.Write(type3_header);

				if (chunk_header->is_extended_timestamp)
				{
					// Write extended timestamp
					byte_stream.WriteBE32(chunk_header->extended_timestamp);
				}
			}
			else
			{
				need_to_write_type3_header = true;
			}

			auto bytes_to_write = std::min(_chunk_size, remaining_payload_length);

			// Write payload
			byte_stream.Write(current_position, bytes_to_write);

			current_position += bytes_to_write;
			remaining_payload_length -= bytes_to_write;
		}
	}

	std::shared_ptr<const ov::Data> ChunkWriter::Serialize(const std::shared_ptr<const ChunkWriteInfo> &chunk_write_info) const
	{
		if (chunk_write_info == nullptr)
		{
			// Invalid parameter
			OV_ASSERT2(false);
			return nullptr;
		}

		auto payload = chunk_write_info->payload;
		auto payload_length = payload->GetLength();

		auto chunk_header = std::make_shared<ChunkHeader>();

		chunk_header->basic_header.format_type = MessageHeaderType::T0;
		chunk_header->basic_header.chunk_stream_id = chunk_write_info->chunk_stream_id;
		chunk_header->message_header.type_0.timestamp = 0;
		chunk_header->message_header.type_0.length = payload_length;
		chunk_header->message_header.type_0.type_id = chunk_write_info->type_id;
		chunk_header->message_header.type_0.stream_id = chunk_write_info->stream_id;

		const auto expected_length = CalculateDataLength(chunk_header, payload_length);
		ov::ByteStream byte_stream(expected_length);

#if DEBUG
		auto start_offset = byte_stream.GetOffset();
#endif	// DEBUG
		Serialize(chunk_write_info, chunk_header, byte_stream);
#if DEBUG
		auto end_offset = byte_stream.GetOffset();
		size_t actual_length = end_offset - start_offset;
		OV_ASSERT(actual_length == expected_length, "Data length mismatch: actual: %zu != expected: %zu", actual_length, expected_length);
#endif	// DEBUG

		return byte_stream.GetDataPointer();
	}
}  // namespace modules::rtmp
