//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2025 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include "./rtmp_datastructure.h"

namespace modules::rtmp
{
	constexpr uint8_t GetBasicHeaderLength(const uint32_t chunk_stream_id)
	{
		switch (chunk_stream_id)
		{
			case 0b000000:
				// Value 0 indicates the 2 byte form and an ID in the range of 64-319
				// (the second byte + 64)
				return 2;

			case 0b000001:
				// Value 1 indicates the 3 byte form and an ID in the range of 64-65599
				// ((the third byte) * 256 + the second byte + 64)
				return 3;

			default:
				// Chunk stream IDs 2-63 can be encoded in the 1-byte version of this field
				if (chunk_stream_id == 2)
				{
					// Chunk Stream ID with value 2 is reserved for low-level protocol control messages and commands
				}
				else
				{
					// Values in the range of 3-63 represent the complete stream ID
				}

				return 1;
		}
	}

	constexpr uint8_t GetMessageHeaderLength(const MessageHeaderType format_type, const bool is_extended_timestamp)
	{
		uint8_t size = 0;

		switch (format_type)
		{
			case MessageHeaderType::T0:
				size = 11;
				break;

			case MessageHeaderType::T1:
				size = 7;
				break;

			case MessageHeaderType::T2:
				size = 3;
				break;

			case MessageHeaderType::T3:
				size = 0;
				break;
		}

		return size + (is_extended_timestamp ? EXTENDED_TIMESTAMP_SIZE : 0);
	}

	// basic header + message header length
	constexpr uint8_t GetChunkHeaderLength(uint32_t chunk_stream_id,
										   MessageHeaderType format_type,
										   bool is_extended_timestamp)
	{
		return GetBasicHeaderLength(chunk_stream_id) +
			   GetMessageHeaderLength(format_type, is_extended_timestamp);
	}
}  // namespace modules::rtmp
