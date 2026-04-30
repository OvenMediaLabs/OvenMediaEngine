//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2025 AirenSoft. All rights reserved.
//
//==============================================================================
#include "rtmp_chunk_parser.h"

#include "../rtmp_private.h"
#include "./rtmp_utilities.h"

namespace modules::rtmp
{
	namespace
	{
		constexpr uint32_t SIGNED_EXTENDED_TIMESTAMP_DELTA_SIGN_BIT = 0x80000000U;
		constexpr int64_t MAX_NEGATIVE_EXTENDED_TIMESTAMP_DELTA_MS	= 10 * 1000;
	}  // namespace

	ChunkParser::ChunkParser(int chunk_size)
	{
		_chunk_size = chunk_size;
	}

	ChunkParser::~ChunkParser()
	{
		Destroy();
	}

	ChunkParser::ParseResult ChunkParser::Parse(const std::shared_ptr<const ov::Data> &data, size_t *bytes_used)
	{
		ov::ByteStream stream(data.get());

		*bytes_used = 0ULL;

		logtp("Trying to parse RTMP chunk from %zu bytes (chunk size: %zu)", stream.Remained(), _chunk_size);

		if (_need_to_parse_new_header)
		{
			// Need to parse new header when parsing for the first time or when reaching the chunk size
			auto parsed_chunk_header = std::make_shared<ChunkHeader>();
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

			logtp("RTMP header is parsed: %s", parsed_chunk_header->ToString().CStr());

			if (_current_message != nullptr)
			{
				auto &current_chunk_header		   = _current_message->header;
				const auto current_chunk_stream_id = current_chunk_header->basic_header.chunk_stream_id;
				const auto new_chunk_stream_id	   = parsed_chunk_header->basic_header.chunk_stream_id;

				if (current_chunk_stream_id != new_chunk_stream_id)
				{
					// If there is a message being parsed, but a discontinuous message comes in, it is put in the map to be parsed later.
					logtt("New chunk stream ID is detected: %u -> %u", current_chunk_stream_id, new_chunk_stream_id);

					_pending_message_map[current_chunk_stream_id] = _current_message;

					if (_pending_message_map.size() > 10)
					{
						logtw("Too many pending RTMP messages: %zu", _pending_message_map.size());
					}

					// Check if there was something being parsed
					auto old_chunk = _pending_message_map.find(new_chunk_stream_id);

					if (old_chunk != _pending_message_map.end())
					{
						logtt("Found pending message for chunk stream ID: %u", new_chunk_stream_id);

						// Just append the data to the message being parsed
						_current_message = old_chunk->second;

						if (parsed_chunk_header->basic_header.format_type != MessageHeaderType::T3)
						{
							logte("Expected Type 3 header, but got: %d", ov::ToUnderlyingType(parsed_chunk_header->basic_header.format_type));
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
					_current_message = std::make_shared<Message>(
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
		logtp("Parsing RTMP Payload (%zu bytes needed)\n%s", _current_message->GetRemainedPayloadSize(), stream.Dump(32).CStr());

		if (_current_message->payload->GetLength() > 0)
		{
			logtp("Append payload to current message payload: %s", _current_message->payload->Dump(32).CStr());
		}
		else
		{
			logtp("No payload in current message");
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

				logtt("New RTMP message is enqueued: %s", current_message_header->ToString().CStr());
				logtp("New RTMP message payload: %s", _current_message->payload->Dump().CStr());
				_current_message = nullptr;
			}
			else
			{
				logtp("Need to parse next chunk (%zu bytes remained to completed current messasge)", _current_message->GetRemainedPayloadSize());
			}

			status					  = ParseResult::Parsed;

			// A new message is completed or the chunk size is reached, so a new header parsing is required.
			_need_to_parse_new_header = true;
		}
		else
		{
			logtp("Need more data to parse payload: %zu bytes (current: %zu)", _current_message->GetRemainedPayloadSize(), stream.Remained());
			status = ParseResult::NeedMoreData;
		}

#if DEBUG
		_total_read_bytes += stream.GetOffset();
#endif	// DEBUG

		*bytes_used = stream.GetOffset();

		return status;
	}

	ChunkParser::ParseResult ChunkParser::ParseBasicHeader(ov::ByteStream &stream, ChunkHeader *chunk_header)
	{
		if (stream.IsEmpty())
		{
			logtp("Need more data to parse basic header");
			return ParseResult::NeedMoreData;
		}

		const auto first_byte			  = stream.Read8();

		auto &basic_header				  = chunk_header->basic_header;

		// Parse basic header
		basic_header.format_type		  = static_cast<MessageHeaderType>((first_byte & 0b11000000) >> 6);
		basic_header.chunk_stream_id	  = (first_byte & 0b00111111);

		chunk_header->basic_header_length = GetBasicHeaderLength(basic_header.chunk_stream_id);

		if (stream.IsRemained(chunk_header->basic_header_length - 1) == false)
		{
			logtp("Need more data to parse basic header: %d bytes needed, current: %zu", (chunk_header->basic_header_length - 1), stream.Remained());
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

	std::shared_ptr<const ChunkHeader> ChunkParser::GetPrecedingChunkHeader(const uint32_t chunk_stream_id)
	{
		auto header = _preceding_chunk_header_map.find(chunk_stream_id);

		if (header == _preceding_chunk_header_map.end())
		{
			return nullptr;
		}

		return header->second;
	}

	bool ChunkParser::IsContinuationChunk(const uint32_t chunk_stream_id) const
	{
		if ((_current_message != nullptr) &&
			(_current_message->header->basic_header.chunk_stream_id == chunk_stream_id) &&
			(_current_message->GetRemainedPayloadSize() > 0))
		{
			return true;
		}

		const auto pending_message = _pending_message_map.find(chunk_stream_id);
		if (pending_message == _pending_message_map.end())
		{
			return false;
		}

		return (pending_message->second != nullptr) && (pending_message->second->GetRemainedPayloadSize() > 0);
	}

	std::optional<uint32_t> ChunkParser::ParseTimestampField(
		const uint32_t stream_id,
		ov::ByteStream &stream,
		ChunkHeader *chunk_header,
		const uint32_t encoded_value)
	{
		if (encoded_value != EXTENDED_TIMESTAMP_VALUE)
		{
			// The 24-bit field already contains the semantic timestamp value, so
			// there is no 32-bit extended field to consume on the wire.
			chunk_header->is_extended_timestamp = false;
			chunk_header->extended_timestamp	= 0U;
			return encoded_value;
		}

		if (stream.IsRemained(EXTENDED_TIMESTAMP_SIZE) == false)
		{
			logtp("Need more data to parse extended timestamp: %d bytes (current: %zu)", EXTENDED_TIMESTAMP_SIZE, stream.Remained());
			return std::nullopt;
		}

		logtt("Extended timestamp is present for stream id: %u", stream_id);

		const uint32_t extended_timestamp	= stream.ReadBE32();

		// The 24-bit field carried the sentinel value, so the real 32-bit value
		// follows immediately and becomes the semantic timestamp field value.
		chunk_header->is_extended_timestamp = true;
		chunk_header->message_header_length += EXTENDED_TIMESTAMP_SIZE;
		chunk_header->extended_timestamp = extended_timestamp;

		return extended_timestamp;
	}

	std::optional<int64_t> ChunkParser::ResolveTimestampDelta(
		const uint32_t stream_id,
		const int64_t preceding_timestamp,
		const uint32_t timestamp_delta,
		const bool is_extended_timestamp) const
	{
		if (is_extended_timestamp && (timestamp_delta >= SIGNED_EXTENDED_TIMESTAMP_DELTA_SIGN_BIT))
		{
			// The sign bit is set in a 32-bit extended delta, so this could be a
			// broken sender that wrote a signed int32_t onto the wire.
			const int64_t signed_timestamp_delta = static_cast<int32_t>(timestamp_delta);

			if ((signed_timestamp_delta < 0) && ((-signed_timestamp_delta) <= MAX_NEGATIVE_EXTENDED_TIMESTAMP_DELTA_MS))
			{
				// Accept only small backward jumps on the compatibility path. Large
				// values are more likely to be legitimate unsigned deltas.
				const int64_t resolved_timestamp = preceding_timestamp + signed_timestamp_delta;

				if (resolved_timestamp < 0)
				{
					// Even on the compatibility path, never allow the absolute
					// timestamp to move below zero.
					logte("Reject signed ext delta: sid=%u prev=%" PRId64 " raw=0x%08x signed=%" PRId64,
						  stream_id,
						  preceding_timestamp,
						  timestamp_delta,
						  signed_timestamp_delta);
					return std::nullopt;
				}

				// This is the non-standard but tolerated case: a small negative
				// extended delta that still resolves to a valid absolute timestamp.
				logtw("Accept signed ext delta: sid=%u prev=%" PRId64 " raw=0x%08x signed=%" PRId64,
					  stream_id,
					  preceding_timestamp,
					  timestamp_delta,
					  signed_timestamp_delta);

				return resolved_timestamp;
			}
		}

		// Standard RTMP behavior: interpret the delta as an unsigned increment.
		return preceding_timestamp + timestamp_delta;
	}

	ChunkParser::ParseResult ChunkParser::ParseMessageHeader(ov::ByteStream &stream, ChunkHeader *chunk_header)
	{
		auto &basic_header					= chunk_header->basic_header;
		auto &message_header				= chunk_header->message_header;
		auto &completed						= chunk_header->completed;

		// Obtains minimum message header size to parse
		chunk_header->message_header_length = GetMessageHeaderLength(basic_header.format_type, false);

		if (stream.IsRemained(chunk_header->message_header_length) == false)
		{
			logtp("Need more data to parse message header: %d bytes (current: %zu)", chunk_header->message_header_length, stream.Remained());
			return ParseResult::NeedMoreData;
		}

		// Parse message header
		chunk_header->is_extended_timestamp						  = false;

		std::shared_ptr<const ChunkHeader> preceding_chunk_header = GetPrecedingChunkHeader(basic_header.chunk_stream_id);

		if (
			(basic_header.format_type != MessageHeaderType::T0) &&
			(preceding_chunk_header == nullptr))
		{
			// T1/T2/T3 message header must have a preceding chunk header
			logte("Could not find preceding chunk header for chunk_stream_id: %u (type: %d)", basic_header.chunk_stream_id, ov::ToUnderlyingType(basic_header.format_type));

#if DEBUG
			logte("chunk_index: %" PRIu64 ", total_read_bytes: %" PRIu64, _chunk_index, _total_read_bytes);
#endif	// DEBUG

			return ParseResult::Error;
		}

		const auto preceding_completed_header = (preceding_chunk_header != nullptr) ? &(preceding_chunk_header->completed) : nullptr;

		// Process extended timestamp if needed
		switch (basic_header.format_type)
		{
			case MessageHeaderType::T0: {
				auto &header					 = message_header.type_0;
				header.timestamp				 = stream.ReadBE24();
				header.length					 = stream.ReadBE24();
				header.type_id					 = static_cast<MessageTypeID>(stream.Read8());
				header.stream_id				 = stream.ReadLE32();

				chunk_header->is_timestamp_delta = false;
				chunk_header->message_length	 = header.length;

				const auto parsed_timestamp		 = ParseTimestampField(
					header.stream_id,
					stream, chunk_header,
					header.timestamp);

				if (parsed_timestamp.has_value() == false)
				{
					return ParseResult::NeedMoreData;
				}

				// Type 0 carries an absolute timestamp. If there is a preceding
				// absolute timestamp on the same chunk stream, unfold the 32-bit
				// serial value into the current absolute epoch with RFC1982 logic.
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

			case MessageHeaderType::T1: {
				auto &header					  = message_header.type_1;
				header.timestamp_delta			  = stream.ReadBE24();
				header.length					  = stream.ReadBE24();
				header.type_id					  = static_cast<MessageTypeID>(stream.Read8());

				chunk_header->is_timestamp_delta  = true;
				chunk_header->message_length	  = header.length;

				const auto parsed_timestamp_delta = ParseTimestampField(
					preceding_completed_header->stream_id,
					stream, chunk_header,
					header.timestamp_delta);

				if (parsed_timestamp_delta.has_value() == false)
				{
					return ParseResult::NeedMoreData;
				}

				const auto resolved_timestamp = ResolveTimestampDelta(
					preceding_completed_header->stream_id,
					preceding_completed_header->timestamp,
					parsed_timestamp_delta.value(),
					chunk_header->is_extended_timestamp);

				if (resolved_timestamp.has_value() == false)
				{
					return ParseResult::Error;
				}

				// Type 1 carries a timestamp delta, not a wrapped absolute timestamp.
				// Once the preceding timestamp has already been unfolded into the
				// current absolute epoch, simple addition is the correct RTMP behavior.
				completed.timestamp		  = resolved_timestamp.value();
				completed.timestamp_delta = parsed_timestamp_delta.value();
				completed.type_id		  = header.type_id;
				completed.stream_id		  = preceding_completed_header->stream_id;

				break;
			}

			case MessageHeaderType::T2: {
				auto &header					  = message_header.type_2;
				header.timestamp_delta			  = stream.ReadBE24();

				chunk_header->is_timestamp_delta  = true;
				chunk_header->message_length	  = preceding_chunk_header->message_length;

				const auto parsed_timestamp_delta = ParseTimestampField(
					preceding_completed_header->stream_id,
					stream, chunk_header,
					header.timestamp_delta);

				if (parsed_timestamp_delta.has_value() == false)
				{
					return ParseResult::NeedMoreData;
				}

				const auto resolved_timestamp = ResolveTimestampDelta(
					preceding_completed_header->stream_id,
					preceding_completed_header->timestamp,
					parsed_timestamp_delta.value(),
					chunk_header->is_extended_timestamp);

				if (resolved_timestamp.has_value() == false)
				{
					return ParseResult::Error;
				}

				// Type 2 is the same timestamp-delta model as Type 1, just with fewer
				// header fields. Do not run RFC1982 rollover logic here.
				completed.timestamp		  = resolved_timestamp.value();
				completed.timestamp_delta = parsed_timestamp_delta.value();
				completed.type_id		  = preceding_completed_header->type_id;
				completed.stream_id		  = preceding_completed_header->stream_id;

				break;
			}

			case MessageHeaderType::T3: {
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
							stream, chunk_header,
							EXTENDED_TIMESTAMP_VALUE);

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
							logtw("Type3 ext mismatch: sid=%u csid=%u origin=%d semantic=%s expected=0x%08x parsed=0x%08x",
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
					// The origin message header is T0.
					const auto parsed_timestamp = ParseTimestampField(
						completed.stream_id,
						stream, chunk_header,
						chunk_header->is_extended_timestamp ? EXTENDED_TIMESTAMP_VALUE : static_cast<uint32_t>(completed.timestamp));

					if (parsed_timestamp.has_value() == false)
					{
						return ParseResult::NeedMoreData;
					}

					// Type 3 inherited from Type 0, so this path still resolves a
					// wrapped absolute timestamp rather than a delta.
					completed.timestamp		  = CalculateRolledTimestamp(completed.stream_id, preceding_completed_header->timestamp, parsed_timestamp.value());
					completed.timestamp_delta = 0U;
				}
				else
				{
					// The origin message header is T1 or T2.
					const auto parsed_timestamp_delta = ParseTimestampField(
						completed.stream_id,
						stream, chunk_header,
						chunk_header->is_extended_timestamp ? EXTENDED_TIMESTAMP_VALUE : completed.timestamp_delta);

					if (parsed_timestamp_delta.has_value() == false)
					{
						return ParseResult::NeedMoreData;
					}

					const auto resolved_timestamp = ResolveTimestampDelta(
						completed.stream_id,
						completed.timestamp,
						parsed_timestamp_delta.value(),
						chunk_header->is_extended_timestamp);

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
			// Keep the parser invariant explicit: downstream code assumes RTMP
			// timestamps are non-negative once a chunk header has been accepted.
			logte("Rejecting RTMP chunk because it produces a negative absolute timestamp: stream_id: %u, chunk_stream_id: %u, type: %d, timestamp: %" PRId64,
				  completed.stream_id,
				  basic_header.chunk_stream_id,
				  ov::ToUnderlyingType(basic_header.format_type),
				  completed.timestamp);
			return ParseResult::Error;
		}

		return ParseResult::Parsed;
	}

	ChunkParser::ParseResult ChunkParser::ParseHeader(ov::ByteStream &stream, ChunkHeader *chunk_header)
	{
		logtp("Parsing RTMP Header\n%s", stream.Dump(16).CStr());

		auto status = ParseBasicHeader(stream, chunk_header);

		if (status != ParseResult::Parsed)
		{
			return status;
		}

		return ParseMessageHeader(stream, chunk_header);
	}

	int64_t ChunkParser::CalculateRolledTimestamp(const uint32_t stream_id, const int64_t last_timestamp, int64_t parsed_timestamp)
	{
		// RTMP timestamps are carried as 32-bit serial numbers on the wire.
		// Keep the serial width, its modulo, and the RFC1982 half-range explicit so
		// the rollover math below reads directly in terms of the specification.
		constexpr int64_t SERIAL_BITS		 = 32;
		constexpr int64_t SERIAL_MODULO		 = (1LL << SERIAL_BITS);
		constexpr int64_t SERIAL_HALF_RANGE	 = (1LL << (SERIAL_BITS - 1));
		// Some senders behave as if the timestamp space were signed int32_t and
		// restart near INT32_MAX -> 0. Keep that compatibility boundary separate
		// from the real RTMP modulo so the fallback path stays clearly scoped.
		constexpr int64_t SIGNED_SERIAL_MODULO = (1LL << (SERIAL_BITS - 1));
		// Only the low 32 bits participate in RFC1982 ordering; higher bits belong
		// to older unfolded epochs reconstructed locally by the receiver.
		constexpr uint64_t SERIAL_VALUE_MASK = 0xFFFFFFFFULL;
		// Some senders appear to reset absolute timestamps at the signed 31-bit
		// boundary instead of RTMP's unsigned 32-bit boundary. Treat that as a
		// compatibility path only when the implied forward delta is very small.
		constexpr int64_t MAX_SIGNED_WRAP_COMPAT_DELTA_MS = 10 * 1000;

		// RFC1982 comparison works on the 32-bit serial value only, not on the full
		// accumulated absolute timestamp. Masking with an unsigned 32-bit pattern
		// intentionally strips any older epoch bits before converting to uint32_t.
		const auto last_serial				 = static_cast<uint32_t>(last_timestamp & SERIAL_VALUE_MASK);
		const auto parsed_serial			 = static_cast<uint32_t>(parsed_timestamp);

		if (last_serial == parsed_serial)
		{
			return last_timestamp;
		}

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

		// This helper is only for absolute timestamp paths (Type 0, or Type 3 that
		// inherits Type 0 semantics). Type 1/2/3 delta paths already operate on an
		// unfolded absolute timestamp plus a delta, so applying RFC1982 again there
		// would be incorrect.
		//
		// Behavior:
		// - if the parsed serial is after the previous serial and wrapped below it,
		//   advance to the next 32-bit epoch
		// - if the parsed serial is not after the previous serial, keep it in the
		//   current/prior epoch so backward Type 0 timestamps remain backward
		const int64_t serial_epoch_base = last_timestamp - static_cast<int64_t>(last_serial);
		int64_t resolved_timestamp		= serial_epoch_base + parsed_serial;
		const int64_t serial_diff		= static_cast<int64_t>(parsed_serial) - last_serial;
		const bool parsed_serial_is_after_previous =
			((serial_diff > 0) && (serial_diff < SERIAL_HALF_RANGE)) ||
			((serial_diff < 0) && ((-serial_diff) > SERIAL_HALF_RANGE));
		// Compatibility path for senders that appear to restart absolute
		// timestamps at INT32_MAX -> 0 instead of UINT32_MAX -> 0. This is not
		// standard RTMP rollover, so only accept it when the implied forward delta
		// is very small.
		const int64_t signed_wrap_forward_delta = SIGNED_SERIAL_MODULO - static_cast<int64_t>(last_serial) + parsed_serial;
		const bool signed_wrap_compat_candidate =
			(parsed_serial_is_after_previous == false) &&
			(last_serial < SIGNED_SERIAL_MODULO) &&
			(parsed_serial < SIGNED_SERIAL_MODULO) &&
			(parsed_serial < last_serial) &&
			(signed_wrap_forward_delta > 0) &&
			(signed_wrap_forward_delta <= MAX_SIGNED_WRAP_COMPAT_DELTA_MS);

		if (parsed_serial_is_after_previous)
		{
			if (resolved_timestamp <= last_timestamp)
			{
				resolved_timestamp += SERIAL_MODULO;
				logtd("Timestamp is rolled forward: last TS: %" PRId64 ", parsed: %" PRIu32 ", resolved: %" PRId64,
					  last_timestamp,
					  parsed_serial,
					  resolved_timestamp);
			}
		}
		// Only reach this branch when the normal RFC1982 32-bit comparison says
		// the parsed serial is not after the previous one.
		else if (signed_wrap_compat_candidate)
		{
			resolved_timestamp = last_timestamp + signed_wrap_forward_delta;
			logtd("Timestamp is rolled forward with signed-31bit compatibility: last TS: %" PRId64 ", parsed: %" PRIu32 ", delta: %" PRId64 ", resolved: %" PRId64,
				  last_timestamp,
				  parsed_serial,
				  signed_wrap_forward_delta,
				  resolved_timestamp);
		}
		else if (resolved_timestamp > last_timestamp)
		{
			resolved_timestamp -= SERIAL_MODULO;
			logti("Timestamp is resolved as backward T0: last TS: %" PRId64 ", parsed: %" PRIu32 ", resolved: %" PRId64,
				  last_timestamp,
				  parsed_serial,
				  resolved_timestamp);
		}

		return resolved_timestamp;
	}

	std::shared_ptr<const Message> ChunkParser::GetMessage()
	{
		return _message_queue.Dequeue(0).value_or(nullptr);
	}

	size_t ChunkParser::GetMessageCount() const
	{
		return _message_queue.Size();
	}

	info::NamePath ChunkParser::GetNamePath() const
	{
		std::lock_guard lock_guard(_name_path_mutex);
		return _name_path;
	}

	void ChunkParser::UpdateNamePath(const info::NamePath &stream_name_path)
	{
		std::lock_guard lock_guard(_name_path_mutex);

		_name_path = stream_name_path;
		_message_queue.SetAlias(ov::String::FormatString("RTMP queue for %s", _name_path.CStr()));
	}

	void ChunkParser::Destroy()
	{
		_preceding_chunk_header_map.clear();

		_message_queue.Stop();
		_message_queue.Clear();
	}
}  // namespace modules::rtmp
