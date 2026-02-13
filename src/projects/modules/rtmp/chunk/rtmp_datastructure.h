//==============================================================================
//
//  RtmpProvider
//
//  Created by Hyunjun Jang
//  Copyright (c) 2019 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <base/ovlibrary/ovlibrary.h>

#include "./rtmp_define.h"

enum class RtmpMessageHeaderType : uint8_t
{
	T0 = 0b00,
	T1 = 0b01,
	T2 = 0b10,
	T3 = 0b11
};

#pragma pack(push, 1)
struct RtmpChunkHeader
{
	static constexpr const size_t EXTENDED_TIMESTAMP_SIZE = 4;

	RtmpChunkHeader()
	{
		::memset(&message_header, 0, sizeof(message_header));
	}

	// +--------------+----------------+--------------------+--------------+
	// | Basic Header | Message Header | Extended Timestamp |  Chunk Data  |
	// +--------------+----------------+--------------------+--------------+
	// |                                                    |
	// |<------------------- Chunk Header ----------------->|
	//                             Chunk Format

	// Indicates whether the timestamp is extended
	bool is_extended_timestamp = false;
	bool is_timestamp_delta = false;

	// Total chunk header size (basic header + message header + extended timestamp)
	uint32_t GetTotalHeaderLength() const
	{
		return basic_header_length + message_header_length + ((is_extended_timestamp) ? sizeof(uint32_t) : 0U);
	}

#if DEBUG
	uint64_t chunk_index = 0ULL;

	uint64_t from_byte_offset = 0ULL;
	mutable uint64_t message_total_bytes = 0ULL;
#endif	// DEBUG

	// 1 or 2 or 3
	uint8_t basic_header_length = 0U;
	uint32_t message_header_length = 0U;
	uint32_t message_length = 0U;

	// uint32_t expected_type_3_header{};
	// The payload size that including type 3 messages
	// uint32_t expected_payload_size = 0U;

	// Basic Header
	struct BasicHeader
	{
		// Chunk type
		RtmpMessageHeaderType format_type;

		// Chunk stream id
		uint32_t chunk_stream_id;
	} basic_header;

	// Extended Timestamp/Timestamp delta
	uint32_t extended_timestamp = 0U;
	static_assert(sizeof(RtmpChunkHeader::extended_timestamp) == EXTENDED_TIMESTAMP_SIZE, "Extended timestamp size must be 4 bytes");

	struct CompletedHeader
	{
		// Accumulated timestamp
		int64_t timestamp = 0LL;
		uint32_t timestamp_delta = 0U;

		RtmpMessageTypeID type_id = RtmpMessageTypeID::Unknown;
		uint32_t stream_id = 0U;
	} completed;

	// Message Header
	union MessageHeader
	{
		// Type 0 chunk headers are 11 bytes long. This type MUST be used at
		// the start of a chunk stream, and whenever the stream timestamp goes
		// backward (e.g., because of a backward seek).
		//
		//  0                   1                   2                   3
		//  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
		// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
		// |                   timestamp                   |message length |
		// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
		// |     message length (cont)     |message type id| msg stream id |
		// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
		// |           message stream id (cont)            |
		// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
		//
		//                   Chunk Message Header - Type 0
		struct Type0
		{
			uint32_t timestamp : 24;
			uint32_t length : 24;
			RtmpMessageTypeID type_id;
			uint32_t stream_id;
		} type_0;

		// Type 1 chunk headers are 7 bytes long. The message stream ID is not
		// included; this chunk takes the same stream ID as the preceding chunk.
		// Streams with variable-sized messages (for example, many video
		// formats) SHOULD use this format for the first chunk of each new
		// message after the first.
		//
		//  0                   1                   2                   3
		//  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
		// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
		// |                timestamp delta                |message length |
		// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
		// |     message length (cont)     |message type id|
		// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
		//
		//                   Chunk Message Header - Type 1
		struct Type1
		{
			uint32_t timestamp_delta : 24;
			uint32_t length : 24;
			RtmpMessageTypeID type_id;
		} type_1;

		// Type 2 chunk headers are 3 bytes long. Neither the stream ID nor the
		// message length is included; this chunk has the same stream ID and
		// message length as the preceding chunk. Streams with constant-sized
		// messages (for example, some audio and data formats) SHOULD use this
		// format for the first chunk of each message after the first.
		//
		//  0                   1                   2                   3
		//  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
		// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
		// |                timestamp delta                |
		// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
		//
		//           Chunk Message Header - Type 2
		struct Type2
		{
			uint32_t timestamp_delta : 24;
		} type_2;

		// Type 3 chunks have no message header. The stream ID, message length
		// and timestamp delta fields are not present; chunks of this type take
		// values from the preceding chunk for the same Chunk Stream ID. When a
		// single message is split into chunks, all chunks of a message except
		// the first one SHOULD use this type.
		struct Type3
		{
		} type_3;
	} message_header;

	ov::String ToString() const
	{
#if DEBUG
		ov::String result = ov::String::FormatString("<RtmpChunkHeader: %p, #%" PRIu64 "(+%" PRIu64 "B, %" PRIu64 " bytes), Basic: ", this, chunk_index, from_byte_offset, message_total_bytes);
#else	// DEBUG
		ov::String result = ov::String::FormatString("<RtmpChunkHeader: %p, Basic: ", this);
#endif	// DEBUG

		if (basic_header_length > 0U)
		{
			result.AppendFormat("<%dB, Type: %d, CSID: %u, TS: %" PRId64 ">, Message: ",
								basic_header_length,
								ov::ToUnderlyingType(basic_header.format_type),
								basic_header.chunk_stream_id,
								completed.timestamp);

			if ((basic_header.format_type == RtmpMessageHeaderType::T3) || (message_header_length > 0U))
			{
				switch (basic_header.format_type)
				{
					case RtmpMessageHeaderType::T0:
						result.AppendFormat("<Type0: %dB, TS: %u, %u bytes, %s (%d), SID: %d>",
											message_header_length,
											message_header.type_0.timestamp,
											message_header.type_0.length,
											StringFromRtmpMessageTypeID(message_header.type_0.type_id),
											ov::ToUnderlyingType(message_header.type_0.type_id),
											message_header.type_0.stream_id);
						break;

					case RtmpMessageHeaderType::T1:
						result.AppendFormat("<Type1: %dB, dTS: %u, %u bytes, %s (%d)>",
											message_header_length,
											message_header.type_1.timestamp_delta,
											message_header.type_1.length,
											StringFromRtmpMessageTypeID(message_header.type_1.type_id),
											ov::ToUnderlyingType(message_header.type_1.type_id));
						break;

					case RtmpMessageHeaderType::T2:
						result.AppendFormat("<Type2: %dB, dTS: %u>",
											message_header_length,
											message_header.type_2.timestamp_delta);
						break;

					case RtmpMessageHeaderType::T3:
						result.AppendFormat("<Type3: %dB>",
											message_header_length);
						break;
				}

				if (is_extended_timestamp)
				{
					result.AppendFormat(", Extended TS: %u", extended_timestamp);
				}

				result.AppendFormat(", Completed: <TS: %" PRId64 ", dTS: %u, %s (%d), SID: %u, Payload: %u bytes>",
									completed.timestamp,
									completed.timestamp_delta,
									StringFromRtmpMessageTypeID(completed.type_id),
									ov::ToUnderlyingType(completed.type_id),
									completed.stream_id,
									message_length);
			}
			else
			{
				result.Append("Not parsed");
			}
		}
		else
		{
			result.Append("Not parsed");
		}

		result.Append('>');

		return result;
	}
};
#pragma pack(pop)

struct RtmpMessage
{
public:
	RtmpMessage(const std::shared_ptr<const RtmpChunkHeader> &header, const std::shared_ptr<ov::Data> &payload)
		: header(header),
		  payload(payload)
	{
		remained_payload_size = header->message_length - payload->GetLength();
	}

	std::shared_ptr<const RtmpChunkHeader> header;

	// Remove const from <const ov::Data> because it should be converted AnnexB or ADTS
	std::shared_ptr<ov::Data> payload;

	bool ReadFromStream(ov::ByteStream &stream, const size_t chunk_size)
	{
		const auto bytes_to_read = std::min(remained_payload_size, chunk_size);

		if (stream.IsRemained(bytes_to_read) == false)
		{
			// Need more data
			return false;
		}

		const off_t buffer_offset = payload->GetLength();

		OV_ASSERT2((buffer_offset + bytes_to_read) <= payload->GetCapacity());
		payload->SetLength(buffer_offset + bytes_to_read);
		remained_payload_size -= bytes_to_read;

		auto buffer = (payload->GetWritableDataAs<uint8_t>() + buffer_offset);

		stream.Read(buffer, bytes_to_read);

		return true;
	}

	size_t GetRemainedPayloadSize() const
	{
		return remained_payload_size;
	}

private:
	size_t remained_payload_size;
};
