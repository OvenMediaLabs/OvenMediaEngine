//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2025 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include "rtmp_define.h"

namespace modules::rtmp
{
	struct ChunkWriteInfo
	{
	protected:
		struct PrivateToken
		{
		};

	public:
		uint32_t chunk_stream_id = 0;
		MessageTypeID type_id = MessageTypeID::Unknown;
		uint32_t stream_id = 0;

		std::shared_ptr<ov::Data> payload;

		ChunkWriteInfo(
			PrivateToken token,
			uint32_t chunk_stream_id,
			MessageTypeID type_id,
			uint32_t stream_id,
			size_t payload_size)
			: chunk_stream_id(chunk_stream_id & 0b00111111),
			  type_id(type_id),
			  stream_id(stream_id),
			  payload((payload_size == 0) ? nullptr : std::make_shared<ov::Data>(payload_size))
		{
			OV_ASSERT(chunk_stream_id == this->chunk_stream_id, "Invalid chunk stream id");
		}

		static std::shared_ptr<ChunkWriteInfo> Create(
			uint32_t chunk_stream_id,
			MessageTypeID type_id,
			uint32_t stream_id,
			size_t payload_size = 0)
		{
			return std::make_shared<ChunkWriteInfo>(
				PrivateToken{},
				chunk_stream_id,
				type_id,
				stream_id,
				payload_size);
		}

		static std::shared_ptr<ChunkWriteInfo> Create(
			ChunkStreamId chunk_stream_id,
			MessageTypeID type_id,
			uint32_t stream_id,
			size_t payload_size = 0)
		{
			return Create(
				ov::ToUnderlyingType(chunk_stream_id),
				type_id,
				stream_id,
				payload_size);
		}

		static std::shared_ptr<ChunkWriteInfo> Create(
			uint32_t chunk_stream_id,
			uint32_t stream_id,
			size_t payload_size = 0)
		{
			return Create(
				chunk_stream_id,
				MessageTypeID::Amf0Command,
				stream_id,
				payload_size);
		}

		static std::shared_ptr<ChunkWriteInfo> Create(uint32_t chunk_stream_id)
		{
			return Create(chunk_stream_id, 0);
		}

		static std::shared_ptr<ChunkWriteInfo> Create(ChunkStreamId chunk_stream_id)
		{
			return Create(ov::ToUnderlyingType(chunk_stream_id), 0);
		}

		void AppendPayload(const void *payload, size_t payload_size)
		{
			if (this->payload == nullptr)
			{
				this->payload = std::make_shared<ov::Data>();
			}

			this->payload->Append(payload, payload_size);
		}

		void AppendPayload(const std::shared_ptr<ov::Data> &payload)
		{
			if (payload == nullptr)
			{
				return;
			}

			return AppendPayload(payload->GetData(), payload->GetLength());
		}

		void AppendPayload(const ov::Data *data)
		{
			if (data == nullptr)
			{
				return;
			}

			AppendPayload(data->GetData(), data->GetLength());
		}

		// This is for enum types
		template <typename T>
		void AppendPayload(const T &payload, typename std::enable_if<std::is_enum<T>::value>::type * = nullptr)
		{
			if constexpr (sizeof(T) == 1)
			{
				AppendPayload(ov::ToUnderlyingType(payload));
			}
			else if constexpr (sizeof(T) == sizeof(uint16_t))
			{
				AppendPayload(ov::HostToBE16(ov::ToUnderlyingType(payload)));
			}
			else if constexpr (sizeof(T) == sizeof(uint24_t))
			{
				AppendPayload(ov::HostToBE24(ov::ToUnderlyingType(payload)));
			}
			else if constexpr (sizeof(T) == sizeof(uint32_t))
			{
				AppendPayload(ov::HostToBE32(ov::ToUnderlyingType(payload)));
			}
			else if constexpr (sizeof(T) == sizeof(uint64_t))
			{
				AppendPayload(ov::HostToBE64(ov::ToUnderlyingType(payload)));
			}
			else
			{
				static_assert([] { return false; }(), "Unsupported type size for process()");
			}
		}

		// This is for non-enum types
		template <typename T>
		void AppendPayload(const T &payload, typename std::enable_if<!std::is_enum<T>::value && std::is_fundamental<T>::value>::type * = nullptr)
		{
			AppendPayload(&payload, sizeof(T));
		}

		const void *GetPayload() const
		{
			if (payload == nullptr)
			{
				return nullptr;
			}

			return payload->GetData();
		}

		size_t GetPayloadLength() const
		{
			if (payload == nullptr)
			{
				return 0;
			}

			return payload->GetLength();
		}
	};
}  // namespace modules::rtmp
