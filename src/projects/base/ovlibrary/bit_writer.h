//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2023 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <base/ovlibrary/ovlibrary.h>
#include <base/ovlibrary/byte_io.h>
#include <math.h>
#include <stdint.h>

#include <memory>
#include <vector>

namespace ov
{
	class BitWriter
	{
	public:
		// Reserve the buffer
		explicit BitWriter(uint32_t reserve_size);

		// Allocate the buffer with the initial value.
		BitWriter(uint32_t size, uint8_t initial_value);
		~BitWriter() = default;

	public:
		bool WriteBits(uint32_t bit_count, uint64_t value);
		// It works only _bit_count is multiple of 8.
		bool WriteData(const uint8_t* data, uint32_t length);

		template <typename T>
		bool WriteBytes(T value, bool big_endian = true)
		{
			// only be used _data is aligned
			if (_bit_count % 8 != 0)
			{
				return false;
			}

			auto current_length = _bit_count / 8;
			if (current_length + sizeof(T) > _data->size())
			{
				_data->resize(current_length + sizeof(T));
			}

			if (big_endian)
			{
				ByteWriter<T>::WriteBigEndian(_data->data() + current_length, value);
			}
			else
			{
				ByteWriter<T>::WriteLittleEndian(_data->data() + current_length, value);
			}

			_bit_count += sizeof(T) * 8;

			return true;
		}

		uint32_t GetBitCount();
		const uint8_t* GetData();
		size_t GetDataSize();
		size_t GetCapacity();
		std::shared_ptr<ov::Data> GetDataObject();

	private:
		std::shared_ptr<std::vector<uint8_t>> _data;
		uint32_t _bit_count;
	};
}  // namespace ov
