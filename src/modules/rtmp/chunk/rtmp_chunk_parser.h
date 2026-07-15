//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2019 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include "../rtmp_chunk_parser_common.h"
#include "rtmp_datastructure.h"
#include "rtmp_define.h"
#include "rtmp_mux_util.h"

class RtmpChunkParser : public RtmpChunkParserCommon<RtmpChunkHeader, RtmpMessage>
{
public:
	using ParseResult = typename RtmpChunkParserCommon<RtmpChunkHeader, RtmpMessage>::ParseResult;

	RtmpChunkParser(size_t chunk_size);
	virtual ~RtmpChunkParser();

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

	std::shared_ptr<const RtmpMessage> GetMessage();
	size_t GetMessageCount() const;

	void SetChunkSize(size_t chunk_size)
	{
		_chunk_size = chunk_size;
	}

	void Destroy();

private:
	/// Returns the most recent completed header for a chunk stream.
	///
	/// @param chunk_stream_id RTMP chunk stream identifier.
	///
	/// @return The preceding parsed header for @p chunk_stream_id, or `nullptr`
	///         if no header has been completed on that chunk stream yet.
	std::shared_ptr<const RtmpChunkHeader> GetPrecedingChunkHeader(const uint32_t chunk_stream_id);

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
	ParseResult ParseBasicHeader(ov::ByteStream &stream, RtmpChunkHeader *chunk_header);

	/// Parses the RTMP message header for an already-parsed basic header.
	///
	/// @param stream Input byte stream positioned at the RTMP message header.
	/// @param chunk_header Destination header object to populate.
	///
	/// @return `Parsed`, `NeedMoreData`, or `Error`.
	ParseResult ParseMessageHeader(ov::ByteStream &stream, RtmpChunkHeader *chunk_header);

	/// Parses both the RTMP basic header and message header.
	///
	/// @param stream Input byte stream positioned at the start of an RTMP header.
	/// @param chunk_header Destination header object to populate.
	///
	/// @return `Parsed`, `NeedMoreData`, or `Error`.
	ParseResult ParseHeader(ov::ByteStream &stream, RtmpChunkHeader *chunk_header);
};
