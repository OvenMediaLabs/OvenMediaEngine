//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2023 AirenSoft. All rights reserved.
//
//==============================================================================
#include "bit_writer.h"
namespace ov
{
	BitWriter::BitWriter(uint32_t reserve_size)
	{
		_data = std::make_shared<std::vector<uint8_t>>();
		_data->reserve(reserve_size);
		_bit_count = 0;
	}

	BitWriter::BitWriter(uint32_t size, uint8_t initial_value)
	{
		_data = std::make_shared<std::vector<uint8_t>>(size, initial_value);
		_bit_count = 0;
	}

	uint32_t BitWriter::GetBitCount()
	{
		return _bit_count;
	}
	const uint8_t* BitWriter::GetData()
	{
		return _data->data();
	}
	size_t BitWriter::GetDataSize()
	{
		return static_cast<size_t>((_bit_count + 7) / 8);
	}
	size_t BitWriter::GetCapacity()
	{
		return _data->size();
	}

	std::shared_ptr<ov::Data> BitWriter::GetDataObject()
	{
		return std::make_shared<ov::Data>(_data->data(), _data->size());
	}

	bool BitWriter::WriteBits(uint32_t bit_count, uint64_t value)
	{
		uint8_t *data = nullptr;
		uint32_t space = 0;

		if (_bit_count + bit_count > _data->size() * 8)
		{
			// resize
			_data->resize((_bit_count + bit_count + 7) / 8);
		}

		data = _data->data();
		data += _bit_count / 8;
		space = 8 - (_bit_count % 8);

		// init the data with 0
		if (space == 8)
		{
			*data = 0;
		}

		while (bit_count)
		{
			uint64_t mask = bit_count == 64 ? 0xFFFFFFFFFFFFFFFF : ((1ULL << bit_count) - 1);

			if (bit_count <= space)
			{
				*data |= ((value & mask) << (space - bit_count));
				_bit_count += bit_count;
				return true;
			}
			else
			{
				*data |= ((value & mask) >> (bit_count - space));
				++data;
				*data = 0; // init

				_bit_count += space;
				bit_count -= space;
				space = 8;
			}
		}

		return true;
	}

	bool BitWriter::WriteData(const uint8_t *data, uint32_t length)
	{
		if (_bit_count % 8 != 0)
		{
			// Writing data is only possible when the bit count is a multiple of 8.
			return false;
		}

		uint8_t *buffer = nullptr;
		auto current_length = _bit_count / 8;

		if (current_length + length > _data->size())
		{
			// resize
			_data->resize(current_length + length);
		}

		buffer = _data->data();

		memcpy((void *)(buffer + current_length), data, length);

		_bit_count += length * 8;

		return true;
	}
}  // namespace ov