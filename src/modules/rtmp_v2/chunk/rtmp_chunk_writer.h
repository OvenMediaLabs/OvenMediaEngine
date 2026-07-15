//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2025 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <map>
#include <memory>

#include "rtmp_chunk_write_info.h"
#include "rtmp_datastructure.h"

namespace modules::rtmp
{
	class ChunkWriter
	{
	public:
		ChunkWriter(size_t chunk_size);

		std::shared_ptr<const ov::Data> Serialize(const std::shared_ptr<const ChunkWriteInfo> &write_info) const;

	protected:
		size_t CalculateDataLength(const std::shared_ptr<const ChunkHeader> &chunk_header, size_t payload_length) const;

		void SerializeBasicHeader(MessageHeaderType format_type, uint32_t chunk_stream_id, ov::ByteStream &stream) const;
		void SerializeBasicHeader(const std::shared_ptr<const ChunkHeader> &chunk_header, ov::ByteStream &stream) const
		{
			return SerializeBasicHeader(chunk_header->basic_header.format_type, chunk_header->basic_header.chunk_stream_id, stream);
		}

		void SerializeMessageHeader(const std::shared_ptr<const ChunkHeader> &chunk_header, ov::ByteStream &stream) const;

		void Serialize(const std::shared_ptr<const ChunkWriteInfo> &write_info,
					   const std::shared_ptr<const ChunkHeader> &chunk_header,
					   ov::ByteStream &byte_stream) const;

	protected:
		size_t _chunk_size = 0;
	};
}  // namespace modules::rtmp
