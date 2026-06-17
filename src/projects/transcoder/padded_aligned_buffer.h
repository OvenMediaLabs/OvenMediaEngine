//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Kwon Keuk Han
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <sys/types.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>

#include <base/mediarouter/media_buffer.h>
#include <base/ovlibrary/ovlibrary.h>

// A 32-byte aligned, padded byte buffer suitable for passing to bitstream parsers
class PaddedAlignedBuffer
{
public:
	PaddedAlignedBuffer()  = default;
	~PaddedAlignedBuffer() = default;

	bool Append(std::shared_ptr<const MediaPacket> packet, const std::shared_ptr<const ov::Data> data)
	{
		if (!packet || !data || data->GetLength() == 0)
		{
			return false;
		}

		if (!Append(data->GetDataAs<uint8_t>(), static_cast<uint32_t>(data->GetLength())))
		{
			return false;
		}

		_remained_size = packet->GetDataLength();
		_offset		   = 0LL;
		_pts		   = (packet->GetPts() == -1LL) ? kNoPtsValue : packet->GetPts();
		_dts		   = (packet->GetDts() == -1LL) ? kNoPtsValue : packet->GetDts();
		_duration	   = (packet->GetDuration() == -1LL) ? kNoPtsValue : packet->GetDuration();

		return true;
	}

	bool Append(const uint8_t* src_data, uint32_t src_size)
	{
		const uint32_t required_size = src_size + kInputBufferPadding;

		// The starting address of the memory buffer passed to the parser must be aligned to 32 bytes,
		// and it must include padding of size kInputBufferPadding to prevent out-of-bounds reads.
		if (!_buffer || _buffer_size < required_size)
		{
			// Allocate a new 32-byte aligned buffer (the previous one, if any, is freed by the deleter).
			uint8_t* raw_ptr = nullptr;
			if (::posix_memalign(reinterpret_cast<void**>(&raw_ptr), 32, required_size) != 0)
			{
				return false;
			}

			if (reinterpret_cast<uintptr_t>(raw_ptr) % 32 != 0)
			{
				loge("PaddedAlignedBuffer", "Buffer pointer is not 32-byte aligned.");
			}

			_buffer		 = BufferPtr(raw_ptr, Deleter());
			_buffer_size = required_size;
		}

		// Copy the data to the aligned buffer and fill the remaining space with zeros.
		std::memcpy(_buffer.get(), src_data, src_size);
		std::memset(_buffer.get() + src_size, 0, _buffer_size - src_size);
		_last_data_size = src_size;

		return true;
	}

	uint8_t* Data() const
	{
		return _buffer.get();
	}

	uint8_t* DataAt(uint32_t offset) const
	{
		if (offset >= _buffer_size)
		{
			return nullptr;
		}
		return _buffer.get() + offset;
	}

	uint8_t* DataAtCurrentOffset()
	{
		if (_remained_size <= 0)
		{
			return nullptr;
		}

		if (_offset >= _buffer_size)
		{
			return nullptr;
		}

		uint8_t* data = _buffer.get() + _offset;

		return data;
	}

	int64_t GetRemainedSize()
	{
		return _remained_size;
	}

	void Advance(int64_t size)
	{
		if (size < 0 || _offset + size > _buffer_size)
		{
			return;
		}

		_offset += size;
		_remained_size -= size;

		if (_remained_size < 0)
		{
			_remained_size = 0;
		}
	}

	uint32_t Capacity() const
	{
		return _buffer_size;
	}

	uint32_t Size() const
	{
		return _last_data_size;
	}

	bool Empty() const
	{
		return !_buffer;
	}

	int64_t GetPts()
	{
		return _pts;
	}

	int64_t GetDts()
	{
		return _dts;
	}

	int64_t GetDuration()
	{
		return _duration;
	}

	void Reset()
	{
		_buffer.reset();
		_buffer_size	 = 0;
		_last_data_size = 0;
		_pts			 = 0;
		_dts			 = 0;
		_duration		 = 0;
		_remained_size	 = 0;
		_offset		 = 0;
	}

private:
	static constexpr int64_t kNoPtsValue = INT64_MIN;
	static constexpr uint32_t kInputBufferPadding = 64;

	struct Deleter
	{
		void operator()(uint8_t* ptr) const
		{
			if (ptr)
			{
				::free(ptr);
			}
		}
	};

	using BufferPtr			 = std::shared_ptr<uint8_t>;
	BufferPtr _buffer		 = nullptr;
	uint32_t _buffer_size	 = 0;
	uint32_t _last_data_size = 0;

	int64_t _pts			 = 0;
	int64_t _dts			 = 0;
	int64_t _duration		 = 0;

	int64_t _remained_size	 = 0;
	off_t _offset			 = 0;
};
