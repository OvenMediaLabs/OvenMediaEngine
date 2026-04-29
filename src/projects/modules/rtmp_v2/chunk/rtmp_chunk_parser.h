//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2025 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <base/info/info.h>

#include <deque>
#include <map>
#include <memory>
#include <optional>

#include "rtmp_datastructure.h"
#include "rtmp_define.h"

namespace modules::rtmp
{
	class ChunkParser
	{
	public:
		enum class ParseResult
		{
			Error,
			NeedMoreData,
			Parsed,
		};

	public:
		ChunkParser(int chunk_size);
		virtual ~ChunkParser();

		/// Parses as much of a single RTMP chunk as possible from the supplied data.
		///
		/// @param data Input bytes beginning at the next unread RTMP chunk boundary.
		/// @param bytes_used Receives the number of bytes consumed from @p data.
		///
		/// @return `Parsed` when a chunk payload step completed, `NeedMoreData` when
		///         more bytes are required, or `Error` when the chunk stream is
		///         malformed.
		///
		/// @note When this returns `NeedMoreData`, @p bytes_used is set to `0` and
		///       the caller must replay the same unconsumed prefix again with more
		///       bytes appended. Header parsing is not resumable from the middle of
		///       a partially received header.
		ParseResult Parse(const std::shared_ptr<const ov::Data> &data, size_t *bytes_used);

		std::shared_ptr<const Message> GetMessage();
		size_t GetMessageCount() const;

		void SetChunkSize(size_t chunk_size)
		{
			_chunk_size = chunk_size;
		}

		// For debugging purposes
		void SetMessageQueueAlias(const char *name)
		{
			_message_queue.SetAlias(name);
		}

		void Destroy();

	private:
		/// Returns the most recent completed header for a chunk stream.
		///
		/// @param chunk_stream_id RTMP chunk stream identifier.
		///
		/// @return The preceding parsed header for @p chunk_stream_id, or `nullptr`
		///         if no header has been completed on that chunk stream yet.
		std::shared_ptr<const ChunkHeader> GetPrecedingChunkHeader(const uint32_t chunk_stream_id);

		/// Checks whether the next header belongs to an unfinished message.
		///
		/// @param chunk_stream_id RTMP chunk stream identifier for the incoming chunk.
		///
		/// @return `true` when the parser is expecting a continuation chunk for the
		///         same chunk stream, including interleaved pending-message cases.
		bool IsContinuationChunk(const uint32_t chunk_stream_id) const;

		/// Parses the RTMP basic header.
		///
		/// @param stream Input byte stream positioned at the RTMP basic header.
		/// @param chunk_header Destination header object to populate.
		///
		/// @return `Parsed`, `NeedMoreData`, or `Error`.
		ParseResult ParseBasicHeader(ov::ByteStream &stream, ChunkHeader *chunk_header);

		/// Parses a 24-bit timestamp field and its optional extended timestamp.
		///
		/// @param stream_id RTMP message stream identifier used for logging/context.
		/// @param stream Input byte stream positioned at the timestamp field.
		/// @param chunk_header Destination header object whose extended-timestamp
		///        metadata is updated on success.
		/// @param encoded_value The already-read 24-bit timestamp or timestamp-delta
		///        field value.
		///
		/// @return The semantic timestamp value on success, or `std::nullopt` when
		///         more bytes are required to finish parsing the field.
		std::optional<uint32_t> ParseTimestampField(
			const uint32_t stream_id,
			ov::ByteStream &stream,
			ChunkHeader *chunk_header,
			const uint32_t encoded_value);

		/// Resolves a timestamp delta against the preceding absolute timestamp.
		///
		/// @param stream_id RTMP message stream identifier used for logging/context.
		/// @param preceding_timestamp Previously resolved absolute timestamp.
		/// @param timestamp_delta Parsed timestamp-delta field value.
		/// @param is_extended_timestamp True when the delta came from the extended
		///        timestamp field.
		///
		/// @return The resolved absolute timestamp, or `std::nullopt` when the
		///         non-standard signed-delta compatibility path would produce an
		///         invalid negative absolute timestamp.
		std::optional<int64_t> ResolveTimestampDelta(
			const uint32_t stream_id,
			const int64_t preceding_timestamp,
			const uint32_t timestamp_delta,
			const bool is_extended_timestamp) const;

		/// Parses the RTMP message header for an already-parsed basic header.
		///
		/// @param stream Input byte stream positioned at the RTMP message header.
		/// @param chunk_header Destination header object to populate.
		///
		/// @return `Parsed`, `NeedMoreData`, or `Error`.
		ParseResult ParseMessageHeader(ov::ByteStream &stream, ChunkHeader *chunk_header);

		/// Parses both the RTMP basic header and message header.
		///
		/// @param stream Input byte stream positioned at the start of an RTMP header.
		/// @param chunk_header Destination header object to populate.
		///
		/// @return `Parsed`, `NeedMoreData`, or `Error`.
		ParseResult ParseHeader(ov::ByteStream &stream, ChunkHeader *chunk_header);

		/// Unfolds a 32-bit absolute RTMP timestamp using RFC1982 serial arithmetic.
		///
		/// @param stream_id RTMP message stream identifier used for logging/context.
		/// @param last_timestamp Previously resolved absolute timestamp.
		/// @param parsed_timestamp Newly parsed 32-bit serial timestamp value.
		///
		/// @return The resolved absolute timestamp in the current or next serial
		///         epoch.
		int64_t CalculateRolledTimestamp(const uint32_t stream_id, const int64_t last_timestamp, int64_t parsed_timestamp);

	private:
#if DEBUG
		uint64_t _chunk_index	   = 0ULL;
		uint64_t _total_read_bytes = 0ULL;
#endif	// DEBUG

		bool _need_to_parse_new_header = true;
		std::shared_ptr<Message> _current_message;
		std::map<uint32_t, std::shared_ptr<Message>> _pending_message_map;
		std::map<uint32_t, std::shared_ptr<const ChunkHeader>> _preceding_chunk_header_map;

		ov::Queue<std::shared_ptr<const Message>> _message_queue{nullptr, 500};
		size_t _chunk_size;
	};
}  // namespace modules::rtmp
