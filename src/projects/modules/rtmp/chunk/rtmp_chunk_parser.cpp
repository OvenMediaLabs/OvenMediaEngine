//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2019 AirenSoft. All rights reserved.
//
//==============================================================================
#include "rtmp_chunk_parser.h"

// `rtmp_private.h` should be included before `rtmp_chunk_parser_helper.h` because it contains `OV_LOG_TAG`.
#include "../rtmp_private.h"
// ====
#include "../rtmp_chunk_parser_helper.h"

namespace
{
	constexpr size_t EXTENDED_TIMESTAMP_SIZE					= 4;
	constexpr size_t EXTENDED_TIMESTAMP_INDICATOR				= 0xFFFFFF;
	// Extended timestamp deltas at or above this value have the sign bit set and
	// would become negative if a sender incorrectly serialized the field from a
	// signed int32_t instead of RTMP's unsigned 32-bit wire format.
	constexpr uint32_t SIGNED_EXTENDED_TIMESTAMP_DELTA_SIGN_BIT = 0x80000000U;
	// Some encoders send extended timestamp deltas as signed 32-bit values
	// even though RTMP defines them as unsigned. Only allow small backward
	// deltas so large valid unsigned deltas are not misread as negative.
	constexpr int64_t MAX_NEGATIVE_EXTENDED_TIMESTAMP_DELTA_MS	= 10 * 1000;
}  // namespace

RtmpChunkParser::RtmpChunkParser(size_t chunk_size)
{
	_chunk_size = chunk_size;
}

RtmpChunkParser::~RtmpChunkParser()
{
	Destroy();
}

RtmpChunkParser::ParseResult RtmpChunkParser::Parse(const std::shared_ptr<const ov::Data> &data, size_t *bytes_used)
{
	ov::ByteStream stream(data.get());

	*bytes_used = 0ULL;

	logap("Trying to parse RTMP chunk from %zu bytes (chunk size: %zu)", stream.Remained(), _chunk_size);

	if (_need_to_parse_new_header)
	{
		// Need to parse new header when parsing for the first time or when reaching the chunk size
		auto parsed_chunk_header = std::make_shared<RtmpChunkHeader>();
		auto status				 = ParseHeader(stream, parsed_chunk_header.get());
		if (status != ParseResult::Parsed)
		{
			// If the header parsing fails, the bytes_used value is not updated to try parsing again from the beginning next time.
			return status;
		}

		_need_to_parse_new_header = false;

#if DEBUG
		parsed_chunk_header->chunk_index	  = _chunk_index;
		parsed_chunk_header->from_byte_offset = _total_read_bytes;
#endif	// DEBUG

		logap("RTMP header is parsed: %s", parsed_chunk_header->ToString().CStr());

		if (_current_message != nullptr)
		{
			auto &current_chunk_header		   = _current_message->header;
			const auto current_chunk_stream_id = current_chunk_header->basic_header.chunk_stream_id;
			const auto new_chunk_stream_id	   = parsed_chunk_header->basic_header.chunk_stream_id;

			if (current_chunk_stream_id != new_chunk_stream_id)
			{
				// If there is a message being parsed, but a discontinuous message comes in, it is put in the map to be parsed later.
				logat("New chunk stream ID is detected: %u -> %u", current_chunk_stream_id, new_chunk_stream_id);

				_pending_message_map[current_chunk_stream_id] = _current_message;

				if (_pending_message_map.size() > 10)
				{
					logaw("Too many pending RTMP messages: %zu", _pending_message_map.size());
				}

				// Check if there was something being parsed
				auto old_chunk = _pending_message_map.find(new_chunk_stream_id);

				if (old_chunk != _pending_message_map.end())
				{
					logat("Found pending message for chunk stream ID: %u", new_chunk_stream_id);

					// Just append the data to the message being parsed
					_current_message = old_chunk->second;

					if (parsed_chunk_header->basic_header.format_type != RtmpMessageHeaderType::T3)
					{
						logae("Expected Type 3 header, but got: %d", ov::ToUnderlyingType(parsed_chunk_header->basic_header.format_type));
					}

					_pending_message_map.erase(new_chunk_stream_id);
				}
				else
				{
					// If there was nothing being parsed, create a new message
					_current_message = nullptr;
				}
			}
			else
			{
				// If a continuous chunk comes in with the same chunk stream ID, it is combined.
			}
		}

		if (_current_message == nullptr)
		{
			auto pending_message = _pending_message_map.find(parsed_chunk_header->basic_header.chunk_stream_id);

			if (pending_message == _pending_message_map.end())
			{
				// If there was nothing being parsed, create a new message
				_current_message = std::make_shared<RtmpMessage>(
					parsed_chunk_header,
					std::make_shared<ov::Data>(parsed_chunk_header->message_length));
			}
			else
			{
				_current_message = pending_message->second;
				_pending_message_map.erase(pending_message);
			}
		}
	}
	else
	{
		// The header has already been parsed. Only the payload part needs to be parsed.
	}

	// Parse the payload

	// RTMP data exists up to the maximum chunk size
	ParseResult status;
	logap("Parsing RTMP Payload (%zu bytes needed)\n%s", _current_message->GetRemainedPayloadSize(), stream.Dump(32).CStr());

	if (_current_message->payload->GetLength() > 0)
	{
		logap("Append payload to current message payload: %s", _current_message->payload->Dump(32).CStr());
	}
	else
	{
		logap("No payload in current message");
	}

	if (_current_message->ReadFromStream(stream, _chunk_size))
	{
		auto &current_message_header													  = _current_message->header;
		_preceding_chunk_header_map[current_message_header->basic_header.chunk_stream_id] = current_message_header;

		if (_current_message->GetRemainedPayloadSize() == 0UL)
		{
			// A new message is completed
			_message_queue.Enqueue(_current_message);

#if DEBUG
			_chunk_index++;
			current_message_header->message_total_bytes = (_total_read_bytes + stream.GetOffset()) - current_message_header->from_byte_offset;
#endif	// DEBUG

			logap("New RTMP message is enqueued: %s, payload:\n%s", current_message_header->ToString().CStr(), _current_message->payload->Dump().CStr());
			_current_message = nullptr;
		}
		else
		{
			logap("Need to parse next chunk (%zu bytes remained to completed current messasge)", _current_message->GetRemainedPayloadSize());
		}

		status					  = ParseResult::Parsed;

		// A new message is completed or the chunk size is reached, so a new header parsing is required.
		_need_to_parse_new_header = true;
	}
	else
	{
		logap("Need more data to parse payload: %zu bytes (current: %zu)", _current_message->GetRemainedPayloadSize(), stream.Remained());
		status = ParseResult::NeedMoreData;
	}

#if DEBUG
	_total_read_bytes += stream.GetOffset();
#endif	// DEBUG

	*bytes_used = stream.GetOffset();

	return status;
}

RtmpChunkParser::ParseResult RtmpChunkParser::ParseBasicHeader(ov::ByteStream &stream, RtmpChunkHeader *chunk_header)
{
	if (stream.IsEmpty())
	{
		logap("Need more data to parse basic header");
		return ParseResult::NeedMoreData;
	}

	const auto first_byte		 = stream.Read8();

	auto &basic_header			 = chunk_header->basic_header;

	// Parse basic header
	basic_header.format_type	 = static_cast<RtmpMessageHeaderType>((first_byte & 0b11000000) >> 6);
	basic_header.chunk_stream_id = (first_byte & 0b00111111);

	switch (basic_header.chunk_stream_id)
	{
		case 0b000000:
			// Value 0 indicates the 2 byte form and an ID in the range of 64-319
			// (the second byte + 64)
			chunk_header->basic_header_length = 2;
			break;

		case 0b000001:
			// Value 1 indicates the 3 byte form and an ID in the range of 64-65599
			// ((the third byte) * 256 + the second byte + 64)
			chunk_header->basic_header_length = 3;
			break;

		default:
			// Chunk stream IDs 2-63 can be encoded in the 1-byte version of this field
			if (basic_header.chunk_stream_id == 2)
			{
				// Chunk Stream ID with value 2 is reserved for low-level protocol control messages and commands
			}
			else
			{
				// Values in the range of 3-63 represent the complete stream ID
			}

			chunk_header->basic_header_length = 1;
			break;
	}

	if (stream.IsRemained(chunk_header->basic_header_length - 1) == false)
	{
		logap("Need more data to parse basic header: %d bytes needed, current: %zu", (chunk_header->basic_header_length - 1), stream.Remained());
		return ParseResult::NeedMoreData;
	}

	switch (basic_header.chunk_stream_id)
	{
		case 0b000000:
			basic_header.chunk_stream_id = stream.Read8() + 64;
			break;

		case 0b000001:
			basic_header.chunk_stream_id = stream.Read16() + 64;
			break;
	}

	return ParseResult::Parsed;
}

std::shared_ptr<const RtmpChunkHeader> RtmpChunkParser::GetPrecedingChunkHeader(const uint32_t chunk_stream_id)
{
	auto header = _preceding_chunk_header_map.find(chunk_stream_id);

	if (header == _preceding_chunk_header_map.end())
	{
		return nullptr;
	}

	return header->second;
}

bool RtmpChunkParser::IsContinuationChunk(const uint32_t chunk_stream_id) const
{
	if ((_current_message != nullptr) &&
		(_current_message->header->basic_header.chunk_stream_id == chunk_stream_id) &&
		(_current_message->GetRemainedPayloadSize() > 0U))
	{
		return true;
	}

	const auto pending_message = _pending_message_map.find(chunk_stream_id);
	if ((pending_message != _pending_message_map.end()) &&
		(pending_message->second != nullptr) &&
		(pending_message->second->GetRemainedPayloadSize() > 0U))
	{
		return true;
	}

	return false;
}

RtmpChunkParser::ParseResult RtmpChunkParser::ParseMessageHeader(ov::ByteStream &stream, RtmpChunkHeader *chunk_header)
{
	auto &basic_header	 = chunk_header->basic_header;
	auto &message_header = chunk_header->message_header;
	auto &completed		 = chunk_header->completed;

	// Obtains minimum message header size to parse
	switch (basic_header.format_type)
	{
		case RtmpMessageHeaderType::T0:
			chunk_header->message_header_length = 11;
			break;
		case RtmpMessageHeaderType::T1:
			chunk_header->message_header_length = 7;
			break;
		case RtmpMessageHeaderType::T2:
			chunk_header->message_header_length = 3;
			break;
		case RtmpMessageHeaderType::T3: {
			chunk_header->message_header_length = 0;
		}
	}

	if (stream.IsRemained(chunk_header->message_header_length) == false)
	{
		logap("Need more data to parse message header: %d bytes (current: %zu)", chunk_header->message_header_length, stream.Remained());
		return ParseResult::NeedMoreData;
	}

	// Parse message header
	chunk_header->is_extended_timestamp							  = false;

	std::shared_ptr<const RtmpChunkHeader> preceding_chunk_header = GetPrecedingChunkHeader(basic_header.chunk_stream_id);

	if (
		(basic_header.format_type != RtmpMessageHeaderType::T0) &&
		(preceding_chunk_header == nullptr))
	{
		// T1/T2/T3 message header must have a preceding chunk header
		logae("Could not find preceding chunk header for chunk_stream_id: %u (type: %d)", basic_header.chunk_stream_id, ov::ToUnderlyingType(basic_header.format_type));

#if DEBUG
		logae("chunk_index: %" PRIu64 ", total_read_bytes: %" PRIu64, _chunk_index, _total_read_bytes);
#endif	// DEBUG

		return ParseResult::Error;
	}

	const auto preceding_completed_header = (preceding_chunk_header != nullptr) ? &(preceding_chunk_header->completed) : nullptr;

	// Process extended timestamp if needed
	switch (basic_header.format_type)
	{
		case RtmpMessageHeaderType::T0: {
			auto &header					 = message_header.type_0;
			header.timestamp				 = stream.ReadBE24();
			header.length					 = stream.ReadBE24();
			header.type_id					 = static_cast<RtmpMessageTypeID>(stream.Read8());
			header.stream_id				 = stream.ReadLE32();

			chunk_header->is_timestamp_delta = false;
			chunk_header->message_length	 = header.length;

			const auto parsed_timestamp		 = ParseTimestampField(
				header.stream_id,
				stream,
				chunk_header->is_extended_timestamp,
				chunk_header->extended_timestamp,
				chunk_header->message_header_length,
				header.timestamp,
				EXTENDED_TIMESTAMP_INDICATOR,
				EXTENDED_TIMESTAMP_SIZE);

			if (parsed_timestamp.has_value() == false)

			{
				return ParseResult::NeedMoreData;
			}

			completed.timestamp		  = parsed_timestamp.value();
			completed.timestamp_delta = 0U;

			if (preceding_completed_header != nullptr)
			{
				completed.timestamp = CalculateRolledTimestamp(header.stream_id, preceding_completed_header->timestamp, completed.timestamp);
			}

			completed.type_id	= header.type_id;
			completed.stream_id = header.stream_id;
			break;
		}

		case RtmpMessageHeaderType::T1: {
			auto &header					  = message_header.type_1;
			header.timestamp_delta			  = stream.ReadBE24();
			header.length					  = stream.ReadBE24();
			header.type_id					  = static_cast<RtmpMessageTypeID>(stream.Read8());

			chunk_header->is_timestamp_delta  = true;
			chunk_header->message_length	  = header.length;

			const auto parsed_timestamp_delta = ParseTimestampField(
				preceding_completed_header->stream_id,
				stream,
				chunk_header->is_extended_timestamp,
				chunk_header->extended_timestamp,
				chunk_header->message_header_length,
				header.timestamp_delta,
				EXTENDED_TIMESTAMP_INDICATOR,
				EXTENDED_TIMESTAMP_SIZE);

			if (parsed_timestamp_delta.has_value() == false)
			{
				return ParseResult::NeedMoreData;
			}

			// Type 1 carries a timestamp delta, not a wrapped absolute timestamp.
			// Once the preceding timestamp has already been unfolded into the
			// current absolute epoch, simple addition is the correct RTMP behavior.
			const auto resolved_timestamp = ResolveTimestampDelta(
				preceding_completed_header->stream_id,
				preceding_completed_header->timestamp,
				parsed_timestamp_delta.value(),
				chunk_header->is_extended_timestamp,
				SIGNED_EXTENDED_TIMESTAMP_DELTA_SIGN_BIT,
				MAX_NEGATIVE_EXTENDED_TIMESTAMP_DELTA_MS);

			if (resolved_timestamp.has_value() == false)
			{
				return ParseResult::Error;
			}

			completed.timestamp		  = resolved_timestamp.value();
			completed.timestamp_delta = parsed_timestamp_delta.value();

			completed.type_id		  = header.type_id;
			completed.stream_id		  = preceding_completed_header->stream_id;

			break;
		}

		case RtmpMessageHeaderType::T2: {
			auto &header					  = message_header.type_2;
			header.timestamp_delta			  = stream.ReadBE24();

			chunk_header->is_timestamp_delta  = true;
			chunk_header->message_length	  = preceding_chunk_header->message_length;

			const auto parsed_timestamp_delta = ParseTimestampField(
				preceding_completed_header->stream_id,
				stream,
				chunk_header->is_extended_timestamp,
				chunk_header->extended_timestamp,
				chunk_header->message_header_length,
				header.timestamp_delta,
				EXTENDED_TIMESTAMP_INDICATOR,
				EXTENDED_TIMESTAMP_SIZE);

			if (parsed_timestamp_delta.has_value() == false)
			{
				return ParseResult::NeedMoreData;
			}

			// Type 2 is the same timestamp-delta model as Type 1, just with fewer
			// header fields. Do not run RFC1982 rollover logic here.
			const auto resolved_timestamp = ResolveTimestampDelta(
				preceding_completed_header->stream_id,
				preceding_completed_header->timestamp,
				parsed_timestamp_delta.value(),
				chunk_header->is_extended_timestamp,
				SIGNED_EXTENDED_TIMESTAMP_DELTA_SIGN_BIT,
				MAX_NEGATIVE_EXTENDED_TIMESTAMP_DELTA_MS);

			if (resolved_timestamp.has_value() == false)
			{
				return ParseResult::Error;
			}

			completed.timestamp		  = resolved_timestamp.value();
			completed.timestamp_delta = parsed_timestamp_delta.value();

			completed.type_id		  = preceding_completed_header->type_id;
			completed.stream_id		  = preceding_completed_header->stream_id;

			break;
		}

		case RtmpMessageHeaderType::T3: {
			chunk_header->is_extended_timestamp = preceding_chunk_header->is_extended_timestamp;
			chunk_header->is_timestamp_delta	= preceding_chunk_header->is_timestamp_delta;
			chunk_header->message_length		= preceding_chunk_header->message_length;

			completed.timestamp					= preceding_completed_header->timestamp;
			completed.timestamp_delta			= preceding_completed_header->timestamp_delta;

			completed.type_id					= preceding_completed_header->type_id;
			completed.stream_id					= preceding_completed_header->stream_id;

			if (IsContinuationChunk(basic_header.chunk_stream_id))
			{
				// Type 3 is also used for continuation chunks of the same message.
				// In that case the message timestamp was already fixed by the first
				// chunk, so do not re-apply either the absolute timestamp or delta.
				// If the preceding chunk used extended timestamp encoding, this
				// continuation chunk still carries the 4-byte field on the wire and
				// it must be consumed even though its semantic value is unchanged.
				//
				// If the preceding chunk was not extended, there is no reliable way
				// to distinguish a malformed extra 4-byte field from normal payload
				// bytes here. Rejecting based on heuristics would risk corrupting
				// valid payload, so the parser only validates the repeated extended
				// field when the message actually uses extended timestamp encoding.
				if (chunk_header->is_extended_timestamp)
				{
					const auto parsed_timestamp_field = ParseTimestampField(
						completed.stream_id,
						stream,
						chunk_header->is_extended_timestamp,
						chunk_header->extended_timestamp,
						chunk_header->message_header_length,
						EXTENDED_TIMESTAMP_INDICATOR,
						EXTENDED_TIMESTAMP_INDICATOR,
						EXTENDED_TIMESTAMP_SIZE);

					if (parsed_timestamp_field.has_value() == false)
					{
						return ParseResult::NeedMoreData;
					}

					// A Type 3 continuation chunk should repeat the same extended
					// timestamp field as the first chunk of the message. If it does
					// not, keep the pre-existing compatibility behavior and continue
					// parsing with the already fixed message timestamp instead of
					// failing the whole session.
					//
					// The log below keeps just enough context to explain whether the
					// stored semantic meaning came from an absolute timestamp path or
					// a timestamp-delta path.
					if (parsed_timestamp_field.value() != preceding_chunk_header->extended_timestamp)
					{
						logaw("Type3 ext mismatch: sid=%u csid=%u origin=%d semantic=%s expected=0x%08x parsed=0x%08x",
							  completed.stream_id,
							  basic_header.chunk_stream_id,
							  ov::ToUnderlyingType(preceding_chunk_header->basic_header.format_type),
							  preceding_chunk_header->is_timestamp_delta ? "delta" : "absolute",
							  preceding_chunk_header->extended_timestamp,
							  parsed_timestamp_field.value());
					}
				}
			}
			else if (chunk_header->is_timestamp_delta == false)
			{
				// The origin message header is T0
				const auto parsed_timestamp = ParseTimestampField(
					completed.stream_id,
					stream,
					chunk_header->is_extended_timestamp,
					chunk_header->extended_timestamp,
					chunk_header->message_header_length,
					chunk_header->is_extended_timestamp ? EXTENDED_TIMESTAMP_INDICATOR : static_cast<uint32_t>(completed.timestamp),
					EXTENDED_TIMESTAMP_INDICATOR,
					EXTENDED_TIMESTAMP_SIZE);

				if (parsed_timestamp.has_value() == false)
				{
					return ParseResult::NeedMoreData;
				}

				completed.timestamp		  = CalculateRolledTimestamp(completed.stream_id, preceding_completed_header->timestamp, parsed_timestamp.value());
				completed.timestamp_delta = 0U;
			}
			else
			{
				// The origin message header is T1 or T2
				const auto parsed_timestamp_delta = ParseTimestampField(
					completed.stream_id,
					stream,
					chunk_header->is_extended_timestamp,
					chunk_header->extended_timestamp,
					chunk_header->message_header_length,
					chunk_header->is_extended_timestamp ? EXTENDED_TIMESTAMP_INDICATOR : completed.timestamp_delta,
					EXTENDED_TIMESTAMP_INDICATOR,
					EXTENDED_TIMESTAMP_SIZE);

				if (parsed_timestamp_delta.has_value() == false)
				{
					return ParseResult::NeedMoreData;
				}

				// Type 3 reuses the timestamp delta semantics of the preceding Type 1
				// or Type 2 header, so it stays on the same absolute+delta path.
				const auto resolved_timestamp = ResolveTimestampDelta(
					completed.stream_id,
					completed.timestamp,
					parsed_timestamp_delta.value(),
					chunk_header->is_extended_timestamp,
					SIGNED_EXTENDED_TIMESTAMP_DELTA_SIGN_BIT,
					MAX_NEGATIVE_EXTENDED_TIMESTAMP_DELTA_MS);

				if (resolved_timestamp.has_value() == false)
				{
					return ParseResult::Error;
				}

				completed.timestamp		  = resolved_timestamp.value();
				completed.timestamp_delta = parsed_timestamp_delta.value();
			}

			break;
		}

		default:
			break;
	}

	if (completed.timestamp < 0)
	{
		logae("Rejecting RTMP chunk because it produces a negative absolute timestamp: stream_id: %u, chunk_stream_id: %u, type: %d, timestamp: %" PRId64,
			  completed.stream_id,
			  basic_header.chunk_stream_id,
			  ov::ToUnderlyingType(basic_header.format_type),
			  completed.timestamp);
		return ParseResult::Error;
	}

	return ParseResult::Parsed;
}

RtmpChunkParser::ParseResult RtmpChunkParser::ParseHeader(ov::ByteStream &stream, RtmpChunkHeader *chunk_header)
{
	logap("Parsing RTMP Header\n%s", stream.Dump(16).CStr());

	auto status = ParseBasicHeader(stream, chunk_header);

	if (status != ParseResult::Parsed)
	{
		return status;
	}

	return ParseMessageHeader(stream, chunk_header);
}

std::shared_ptr<const RtmpMessage> RtmpChunkParser::GetMessage()
{
	if (_message_queue.IsEmpty())
	{
		return nullptr;
	}

	auto item = _message_queue.Dequeue();

	if (item.has_value())
	{
		return item.value();
	}

	return nullptr;
}

size_t RtmpChunkParser::GetMessageCount() const
{
	return _message_queue.Size();
}

void RtmpChunkParser::Destroy()
{
	_preceding_chunk_header_map.clear();

	_message_queue.Stop();
	_message_queue.Clear();
}
