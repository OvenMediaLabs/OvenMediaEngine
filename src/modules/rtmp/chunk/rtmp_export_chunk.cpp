//==============================================================================
//
//  RtmpProvider
//
//  Created by Jaejong Bong
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================

#include "rtmp_export_chunk.h"

//====================================================================================================
// 생성자
//====================================================================================================
RtmpExportChunk::RtmpExportChunk(bool compress_header, int chunk_size)
{
	_stream_map.clear();
	_compress_header = compress_header;
	_chunk_size = chunk_size;
}

//====================================================================================================
// 소멸자
//====================================================================================================
RtmpExportChunk::~RtmpExportChunk()
{
	Destroy();
}

//====================================================================================================
// 종료
//====================================================================================================
void RtmpExportChunk::Destroy()
{
	_stream_map.clear();
}

//====================================================================================================
// Export 스트림 획득
//====================================================================================================
std::shared_ptr<ExportStream> RtmpExportChunk::GetStream(uint32_t chunk_stream_id)
{
	auto item = _stream_map.find(chunk_stream_id);

	if (item != _stream_map.end())
	{
		return item->second;
	}

	// 새로 등록하기 위해 메모리 할당
	auto stream = std::make_shared<ExportStream>();

	// 등록
	_stream_map.insert(std::pair<uint32_t, std::shared_ptr<ExportStream>>(chunk_stream_id, stream));

	return stream;
}

//====================================================================================================
// Export 청크 헤더 설정
// - Type3는 확장 헤더 확인 못함 - 이전 정보를 기반으로 이후에 확인
// - 압축 헤더 : 프레임이 정상 적으로 않나오는 상황에서 헤더 압축 사용하면 타임스템프 전달이 정상적으로 않됨
//====================================================================================================
std::shared_ptr<RtmpChunkHeader> RtmpExportChunk::GetChunkHeader(std::shared_ptr<ExportStream> &stream, const std::shared_ptr<const RtmpMuxMessageHeader> &message_header)
{
	auto chunk_header = std::make_shared<RtmpChunkHeader>();

	// type 2
	if (_compress_header &&
		stream->message_header->chunk_stream_id == message_header->chunk_stream_id &&
		stream->message_header->body_size == message_header->body_size &&
		stream->message_header->stream_id == message_header->stream_id &&
		stream->message_header->type_id == message_header->type_id)
	{
		chunk_header->basic_header.format_type = RtmpMessageHeaderType::T2;
		chunk_header->basic_header.chunk_stream_id = message_header->chunk_stream_id;
		chunk_header->message_header.type_2.timestamp_delta = message_header->timestamp - stream->message_header->timestamp;

		if (chunk_header->message_header.type_2.timestamp_delta >= RTMP_EXTEND_TIMESTAMP)
		{
			chunk_header->is_extended_timestamp = true;
		}
	}
	// type 1
	else if (_compress_header &&
			 stream->message_header->chunk_stream_id == message_header->chunk_stream_id &&
			 stream->message_header->stream_id == message_header->stream_id)
	{
		chunk_header->basic_header.format_type = RtmpMessageHeaderType::T1;
		chunk_header->basic_header.chunk_stream_id = message_header->chunk_stream_id;
		chunk_header->message_header.type_1.timestamp_delta = message_header->timestamp - stream->message_header->timestamp;
		chunk_header->message_header.type_1.length = message_header->body_size;
		chunk_header->message_header.type_1.type_id = message_header->type_id;

		if (chunk_header->message_header.type_1.timestamp_delta >= RTMP_EXTEND_TIMESTAMP)
		{
			chunk_header->is_extended_timestamp = true;
		}
	}
	// type_0  or none compress
	else
	{
		chunk_header->basic_header.format_type = RtmpMessageHeaderType::T0;
		chunk_header->basic_header.chunk_stream_id = message_header->chunk_stream_id;
		chunk_header->message_header.type_0.timestamp = message_header->timestamp;
		chunk_header->message_header.type_0.length = message_header->body_size;
		chunk_header->message_header.type_0.type_id = message_header->type_id;
		chunk_header->message_header.type_0.stream_id = message_header->stream_id;

		if (chunk_header->message_header.type_0.timestamp >= RTMP_EXTEND_TIMESTAMP)
		{
			chunk_header->is_extended_timestamp = true;
		}
	}

	return chunk_header;
}

//====================================================================================================
// Export 스트림
//====================================================================================================
std::shared_ptr<std::vector<uint8_t>> RtmpExportChunk::ExportStreamData(const std::shared_ptr<const RtmpMuxMessageHeader> &message_header, const uint8_t *chunk_data, size_t chunk_size)
{
	int buffer_size = 0;
	int export_size = 0;
	uint32_t type3_time = 0;

	// 파라미터 체크
	if (message_header == nullptr || message_header->chunk_stream_id < 2)
	{
		return nullptr;
	}

	// 스트림 찾기
	auto stream = GetStream(message_header->chunk_stream_id);

	// Chunk Header 구하기
	auto chunk_header = GetChunkHeader(stream, message_header);

	// 버퍼 크기 설정(최대치 계산)
	buffer_size = RTMP_PACKET_HEADER_SIZE_MAX + GetChunkDataRawSize(_chunk_size, message_header->chunk_stream_id, message_header->body_size, chunk_header->is_extended_timestamp);

	switch (chunk_header->basic_header.format_type)
	{
		case RtmpMessageHeaderType::T0:
			type3_time = chunk_header->message_header.type_0.timestamp;
			break;

		case RtmpMessageHeaderType::T1:
			type3_time = chunk_header->message_header.type_1.timestamp_delta;
			break;

		case RtmpMessageHeaderType::T2:
			type3_time = chunk_header->message_header.type_2.timestamp_delta;
			break;

		case RtmpMessageHeaderType::T3:
			break;
	}

	auto export_data = std::make_shared<std::vector<uint8_t>>(buffer_size);

	// Chunk Stream 인코딩
	export_size += GetChunkHeaderRaw(chunk_header, export_data->data(), chunk_header->is_extended_timestamp);
	export_size += GetChunkDataRaw(_chunk_size, message_header->chunk_stream_id, chunk_data, chunk_size, export_data->data() + export_size, chunk_header->is_extended_timestamp, type3_time);
	export_data->resize(export_size);

	// 스트림 Message 헤더 정보 갱신
	stream->timestamp_delta = message_header->timestamp - stream->message_header->timestamp;
	stream->message_header = message_header;

	return export_data;
}
