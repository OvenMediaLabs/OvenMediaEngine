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

		enum class ParseResultForExtendedTimestamp
		{
			NeedMoreData,
			Extended,
			NotExtended,
		};

	public:
		ChunkParser(int chunk_size);
		virtual ~ChunkParser();

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
		std::shared_ptr<const ChunkHeader> GetPrecedingChunkHeader(const uint32_t chunk_stream_id);

		ParseResult ParseBasicHeader(ov::ByteStream &stream, ChunkHeader *chunk_header);
		ParseResultForExtendedTimestamp ParseExtendedTimestamp(
			const uint32_t stream_id,
			ov::ByteStream &stream,
			ChunkHeader *chunk_header,
			const int64_t timestamp,
			ChunkHeader::CompletedHeader *completed_header);
		ParseResultForExtendedTimestamp ParseExtendedTimestampDelta(
			const uint32_t stream_id,
			ov::ByteStream &stream,
			ChunkHeader *chunk_header,
			const int64_t preceding_timestamp,
			const int64_t timestamp_delta,
			ChunkHeader::CompletedHeader *completed_header);
		ParseResult ParseMessageHeader(ov::ByteStream &stream, ChunkHeader *chunk_header);
		ParseResult ParseHeader(ov::ByteStream &stream, ChunkHeader *chunk_header);

		int64_t CalculateRolledTimestamp(const uint32_t stream_id, const int64_t last_timestamp, int64_t parsed_timestamp);

		void UpdateAppStreamName();

	private:
#if DEBUG
		uint64_t _chunk_index = 0ULL;
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
