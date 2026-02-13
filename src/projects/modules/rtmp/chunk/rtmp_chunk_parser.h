//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2019 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <base/info/info.h>

#include <deque>
#include <map>
#include <memory>

#include "rtmp_datastructure.h"
#include "rtmp_define.h"
#include "rtmp_mux_util.h"

class RtmpChunkParser
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
	RtmpChunkParser(size_t chunk_size);
	virtual ~RtmpChunkParser();

	ParseResult Parse(const std::shared_ptr<const ov::Data> &data, size_t *bytes_used);

	std::shared_ptr<const RtmpMessage> GetMessage();
	size_t GetMessageCount() const;

	void SetChunkSize(size_t chunk_size)
	{
		_chunk_size = chunk_size;
	}

	info::NamePath GetNamePath() const;
	void UpdateNamePath(const info::NamePath &stream_name_path);

	void Destroy();

private:
	std::shared_ptr<const RtmpChunkHeader> GetPrecedingChunkHeader(const uint32_t chunk_stream_id);

	ParseResult ParseBasicHeader(ov::ByteStream &stream, RtmpChunkHeader *chunk_header);
	ParseResultForExtendedTimestamp ParseExtendedTimestamp(
		const uint32_t stream_id,
		ov::ByteStream &stream,
		RtmpChunkHeader *chunk_header,
		const int64_t timestamp,
		RtmpChunkHeader::CompletedHeader *completed_header);
	ParseResultForExtendedTimestamp ParseExtendedTimestampDelta(
		const uint32_t stream_id,
		ov::ByteStream &stream,
		RtmpChunkHeader *chunk_header,
		const int64_t preceding_timestamp,
		const int64_t timestamp_delta,
		RtmpChunkHeader::CompletedHeader *completed_header);
	ParseResult ParseMessageHeader(ov::ByteStream &stream, RtmpChunkHeader *chunk_header);
	ParseResult ParseHeader(ov::ByteStream &stream, RtmpChunkHeader *chunk_header);

	int64_t CalculateRolledTimestamp(const uint32_t stream_id, const int64_t last_timestamp, int64_t parsed_timestamp);

private:
#if DEBUG
	uint64_t _chunk_index = 0ULL;
	uint64_t _total_read_bytes = 0ULL;
#endif	// DEBUG

	bool _need_to_parse_new_header = true;
	std::shared_ptr<RtmpMessage> _current_message;
	std::map<uint32_t, std::shared_ptr<RtmpMessage>> _pending_message_map;
	std::map<uint32_t, std::shared_ptr<const RtmpChunkHeader>> _preceding_chunk_header_map;

	ov::Queue<std::shared_ptr<const RtmpMessage>> _message_queue{nullptr, 500};
	size_t _chunk_size;

	mutable std::mutex _name_path_mutex;
	info::NamePath _name_path;
};
