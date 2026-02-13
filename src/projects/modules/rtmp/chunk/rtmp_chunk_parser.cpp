//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2019 AirenSoft. All rights reserved.
//
//==============================================================================
#include "rtmp_chunk_parser.h"

#include "../rtmp_private.h"

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
		auto status = ParseHeader(stream, parsed_chunk_header.get());
		if (status != ParseResult::Parsed)
		{
			// If the header parsing fails, the bytes_used value is not updated to try parsing again from the beginning next time.
			return status;
		}

		_need_to_parse_new_header = false;

#if DEBUG
		parsed_chunk_header->chunk_index = _chunk_index;
		parsed_chunk_header->from_byte_offset = _total_read_bytes;
#endif	// DEBUG

		logap("RTMP header is parsed: %s", parsed_chunk_header->ToString().CStr());

		if (_current_message != nullptr)
		{
			auto &current_chunk_header = _current_message->header;
			const auto current_chunk_stream_id = current_chunk_header->basic_header.chunk_stream_id;
			const auto new_chunk_stream_id = parsed_chunk_header->basic_header.chunk_stream_id;

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
		auto &current_message_header = _current_message->header;
		_preceding_chunk_header_map[current_message_header->basic_header.chunk_stream_id] = current_message_header;

		if (_current_message->GetRemainedPayloadSize() == 0UL)
		{
			// A new message is completed
			_message_queue.Enqueue(_current_message);

#if DEBUG
			_chunk_index++;
			current_message_header->message_total_bytes = (_total_read_bytes + stream.GetOffset()) - current_message_header->from_byte_offset;
#endif	// DEBUG

			logap("New RTMP message is enqueued: %s, payload:\n%s",current_message_header->ToString().CStr(),  _current_message->payload->Dump().CStr());
			_current_message = nullptr;
		}
		else
		{
			logap("Need to parse next chunk (%zu bytes remained to completed current messasge)", _current_message->GetRemainedPayloadSize());
		}

		status = ParseResult::Parsed;

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

	const auto first_byte = stream.Read8();

	auto &basic_header = chunk_header->basic_header;

	// Parse basic header
	basic_header.format_type = static_cast<RtmpMessageHeaderType>((first_byte & 0b11000000) >> 6);
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

RtmpChunkParser::ParseResultForExtendedTimestamp RtmpChunkParser::ParseExtendedTimestamp(
	const uint32_t stream_id,
	ov::ByteStream &stream,
	RtmpChunkHeader *chunk_header,
	const int64_t timestamp,
	RtmpChunkHeader::CompletedHeader *completed_header)
{
	if (timestamp != 0xFFFFFF)
	{
		chunk_header->is_extended_timestamp = false;

		completed_header->timestamp = timestamp;
		completed_header->timestamp_delta = 0U;

		return ParseResultForExtendedTimestamp::NotExtended;
	}

	if (stream.IsRemained(RtmpChunkHeader::EXTENDED_TIMESTAMP_SIZE) == false)
	{
		logap("Need more data to parse extended timestamp: %d bytes (current: %zu)", RtmpChunkHeader::EXTENDED_TIMESTAMP_SIZE, stream.Remained());
		return ParseResultForExtendedTimestamp::NeedMoreData;
	}

	logat("Extended timestamp is present for stream id: %u", stream_id);

	int64_t extended_timestamp = stream.ReadBE32();

	chunk_header->is_extended_timestamp = true;
	chunk_header->message_header_length += RtmpChunkHeader::EXTENDED_TIMESTAMP_SIZE;

	completed_header->timestamp = extended_timestamp;
	completed_header->timestamp_delta = 0U;

	return ParseResultForExtendedTimestamp::Extended;
}

RtmpChunkParser::ParseResultForExtendedTimestamp RtmpChunkParser::ParseExtendedTimestampDelta(
	const uint32_t stream_id,
	ov::ByteStream &stream,
	RtmpChunkHeader *chunk_header,
	const int64_t preceding_timestamp,
	const int64_t timestamp_delta,
	RtmpChunkHeader::CompletedHeader *completed_header)
{
	if (timestamp_delta != 0xFFFFFF)
	{
		chunk_header->is_extended_timestamp = false;

		completed_header->timestamp = preceding_timestamp + timestamp_delta;
		completed_header->timestamp_delta = timestamp_delta;

		return ParseResultForExtendedTimestamp::NotExtended;
	}

	if (stream.IsRemained(RtmpChunkHeader::EXTENDED_TIMESTAMP_SIZE) == false)
	{
		logap("Need more data to parse extended timestamp delta: %d bytes (current: %zu)", RtmpChunkHeader::EXTENDED_TIMESTAMP_SIZE, stream.Remained());
		return ParseResultForExtendedTimestamp::NeedMoreData;
	}

	logat("Extended timestamp delta is present for stream id: %u", stream_id);

	OV_ASSERT(RtmpChunkHeader::EXTENDED_TIMESTAMP_SIZE == 4, "Extended timestamp delta size must be 4 bytes");

	int64_t extended_timestamp_delta = stream.ReadBE32();

	chunk_header->is_extended_timestamp = true;
	chunk_header->message_header_length += RtmpChunkHeader::EXTENDED_TIMESTAMP_SIZE;

	completed_header->timestamp = preceding_timestamp + extended_timestamp_delta;
	completed_header->timestamp_delta = extended_timestamp_delta;

	return ParseResultForExtendedTimestamp::Extended;
}

RtmpChunkParser::ParseResult RtmpChunkParser::ParseMessageHeader(ov::ByteStream &stream, RtmpChunkHeader *chunk_header)
{
	auto &basic_header = chunk_header->basic_header;
	auto &message_header = chunk_header->message_header;
	auto &completed = chunk_header->completed;

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
	chunk_header->is_extended_timestamp = false;

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
			auto &header = message_header.type_0;
			header.timestamp = stream.ReadBE24();
			header.length = stream.ReadBE24();
			header.type_id = static_cast<RtmpMessageTypeID>(stream.Read8());
			header.stream_id = stream.ReadLE32();

			chunk_header->is_timestamp_delta = false;
			chunk_header->message_length = header.length;

			auto result = ParseExtendedTimestamp(
				header.stream_id,
				stream, chunk_header,
				header.timestamp, &completed);

			if (result == ParseResultForExtendedTimestamp::NeedMoreData)
			{
				return ParseResult::NeedMoreData;
			}

			if (preceding_completed_header != nullptr)
			{
				completed.timestamp = CalculateRolledTimestamp(header.stream_id, preceding_completed_header->timestamp, completed.timestamp);
			}

			completed.type_id = header.type_id;
			completed.stream_id = header.stream_id;
			break;
		}

		case RtmpMessageHeaderType::T1: {
			auto &header = message_header.type_1;
			header.timestamp_delta = stream.ReadBE24();
			header.length = stream.ReadBE24();
			header.type_id = static_cast<RtmpMessageTypeID>(stream.Read8());

			chunk_header->is_timestamp_delta = true;
			chunk_header->message_length = header.length;

			auto result = ParseExtendedTimestampDelta(
				preceding_completed_header->stream_id,
				stream, chunk_header,
				preceding_completed_header->timestamp,
				header.timestamp_delta,
				&completed);

			if (result == ParseResultForExtendedTimestamp::NeedMoreData)
			{
				return ParseResult::NeedMoreData;
			}

			completed.type_id = header.type_id;
			completed.stream_id = preceding_completed_header->stream_id;

			break;
		}

		case RtmpMessageHeaderType::T2: {
			auto &header = message_header.type_2;
			header.timestamp_delta = stream.ReadBE24();

			chunk_header->is_timestamp_delta = true;
			chunk_header->message_length = preceding_chunk_header->message_length;

			auto result = ParseExtendedTimestampDelta(
				preceding_completed_header->stream_id,
				stream, chunk_header,
				preceding_completed_header->timestamp,
				header.timestamp_delta,
				&completed);

			if (result == ParseResultForExtendedTimestamp::NeedMoreData)
			{
				return ParseResult::NeedMoreData;
			}

			completed.type_id = preceding_completed_header->type_id;
			completed.stream_id = preceding_completed_header->stream_id;

			break;
		}

		case RtmpMessageHeaderType::T3: {
			chunk_header->is_extended_timestamp = preceding_chunk_header->is_extended_timestamp;
			chunk_header->is_timestamp_delta = preceding_chunk_header->is_timestamp_delta;
			chunk_header->message_length = preceding_chunk_header->message_length;

			completed.timestamp = preceding_completed_header->timestamp;
			completed.timestamp_delta = preceding_completed_header->timestamp_delta;

			completed.type_id = preceding_completed_header->type_id;
			completed.stream_id = preceding_completed_header->stream_id;

			ParseResultForExtendedTimestamp result;

			if (chunk_header->is_timestamp_delta == false)
			{
				// The origin message header is T0
				result = ParseExtendedTimestamp(
					completed.stream_id,
					stream, chunk_header,
					chunk_header->is_extended_timestamp ? 0xffffff : completed.timestamp, &completed);

				if (result != ParseResultForExtendedTimestamp::NeedMoreData)
				{
					completed.timestamp = CalculateRolledTimestamp(completed.stream_id, preceding_completed_header->timestamp, completed.timestamp);
				}
			}
			else
			{
				// The origin message header is T1 or T2
				result = ParseExtendedTimestampDelta(
					completed.stream_id,
					stream, chunk_header,
					completed.timestamp, chunk_header->is_extended_timestamp ? 0xffffff : completed.timestamp_delta, &completed);
			}

			if (result == ParseResultForExtendedTimestamp::NeedMoreData)
			{
				return ParseResult::NeedMoreData;
			}

			break;
		}

		default:
			break;
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

int64_t RtmpChunkParser::CalculateRolledTimestamp(const uint32_t stream_id, const int64_t last_timestamp, int64_t parsed_timestamp)
{
	const static int64_t SERIAL_BITS = 31;
	int64_t new_timestamp = parsed_timestamp;
	int64_t delta = ::llabs(last_timestamp - parsed_timestamp);

	// RTMP specification
	//
	// Because timestamps are 32 bits long, they roll over every 49 days, 17
	// hours, 2 minutes and 47.296 seconds. Because streams are allowed to
	// run continuously, potentially for years on end, an RTMP application
	// SHOULD use serial number arithmetic [RFC1982] when processing
	// timestamps, and SHOULD be capable of handling wraparound. For
	// example, an application assumes that all adjacent timestamps are
	// within 2^31 - 1 milliseconds of each other, so 10000 comes after
	// 4000000000, and 3000000000 comes before 4000000000.

	// completed.timestamp calculated from this formula (https://tools.ietf.org/html/rfc1982#section-3.1):
	//
	// Serial numbers may be incremented by the addition of a positive
	// integer n, where n is taken from the range of integers
	// [0 .. (2^(SERIAL_BITS - 1) - 1)].  For a sequence number s, the
	// result of such an addition, s', is defined as
	//
	//                 s' = (s + n) modulo (2 ^ SERIAL_BITS)
	//
	// where the addition and modulus operations here act upon values that
	// are non-negative values of unbounded size in the usual ways of
	// integer arithmetic.

	// Check if the timestamp is an adjacent timestamp
	if (delta <= ((1LL << (SERIAL_BITS - 1)) - 1))
	{
		// Adjacent timestamp - No need to roll timestamp
	}
	else
	{
		// Non-adjacent timestamp - Need to roll timestamp
		new_timestamp = last_timestamp + (1LL << SERIAL_BITS) - (last_timestamp % (1LL << SERIAL_BITS)) + parsed_timestamp;

		logai("Timestamp is rolled: last TS: %" PRId64 ", parsed: %" PRId64 ", new: %" PRId64,
			  last_timestamp,
			  parsed_timestamp,
			  new_timestamp);
	}

	return new_timestamp;
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

info::NamePath RtmpChunkParser::GetNamePath() const
{
	std::lock_guard lock_guard(_name_path_mutex);
	return _name_path;
}

void RtmpChunkParser::UpdateNamePath(const info::NamePath &stream_name_path)
{
	std::lock_guard lock_guard(_name_path_mutex);

	_name_path = stream_name_path;
	_message_queue.SetAlias(ov::String::FormatString("RTMP queue for %s", _name_path.CStr()));
}

void RtmpChunkParser::Destroy()
{
	_preceding_chunk_header_map.clear();

	_message_queue.Stop();
	_message_queue.Clear();
}