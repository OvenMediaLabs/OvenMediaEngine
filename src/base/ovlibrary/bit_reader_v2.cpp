//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2025 AirenSoft. All rights reserved.
//
//==============================================================================
#include "./bit_reader_v2.h"

#define OV_LOG_TAG "BitReader"

namespace ov
{
	BitReader::BitReader(const void *buffer, size_t length)
		: _data(std::make_shared<Data>(buffer, length, true))
	{
		SetBitOffset(0);
	}

	BitReader::BitReader(const std::shared_ptr<const ov::Data> &data)
		: _data(data)
	{
		SetBitOffset(0);
	}

	bool BitReader::SkipBits(const size_t bits_to_skip)
	{
		if (bits_to_skip > _bits_remaining)
		{
			return false;
		}

		auto next_bits = _offset_of_current_byte + bits_to_skip;
		auto next_bytes = next_bits / 8;

		_current += next_bytes;
		_offset_of_current_byte = next_bits % 8;
		_bits_read += bits_to_skip;
		_bits_remaining -= bits_to_skip;

		return true;
	}

	bool BitReader::SetBitOffset(const size_t offset)
	{
		auto data_length = (_data->GetLength() * 8);

		if (offset > data_length)
		{
			return false;
		}

		auto byte_offset = offset / 8;

		_current = _data->GetDataAs<uint8_t>() + byte_offset;
		_offset_of_current_byte = offset % 8;
		_bits_read = offset;
		_bits_remaining = data_length - offset;

		return true;
	}

	bool BitReader::SetByteOffset(const size_t offset)
	{
		return SetBitOffset(offset * 8);
	}

	bool BitReader::SkipBytes(const size_t bytes_to_skip)
	{
		return SkipBits(bytes_to_skip * 8);
	}

	void BitReader::ReadBitsInternal(void *buffer, const size_t length_in_bytes, const size_t bits_to_read)
	{
		auto length_in_bit = length_in_bytes * 8;

		if (bits_to_read > length_in_bit)
		{
			throw BitReaderError(BitReaderError::Code::NotEnoughSpaceInBuffer, "Not enough space in the buffer");
			OV_ASSERT2(false);
		}

		auto bit_offset = length_in_bit - bits_to_read;
		auto byte_offset = (bit_offset / 8);
		bit_offset %= 8;

		ReadBitsInternal(static_cast<uint8_t *>(buffer) + byte_offset, length_in_bit - (byte_offset * 8), static_cast<uint8_t>(bit_offset), bits_to_read);
	}

	void BitReader::ReadBitsInternal(void *buffer, const size_t length_in_bit, const uint8_t bit_offset, const size_t bits_to_read)
	{
		if (bits_to_read > _bits_remaining)
		{
			// Not enough bits to read
			throw BitReaderError(BitReaderError::Code::NotEnoughBitsToRead, "Not enough bits to read");
		}

		const auto bits_copied = BitMemcpy(
			_current, _bits_remaining, _offset_of_current_byte,
			buffer, length_in_bit, bit_offset, bits_to_read);

		if (bits_copied == bits_to_read)
		{
			SkipBits(bits_copied);
			return;
		}

		OV_ASSERT(false, "Failed to read bits: %zu bits to read, but %zu bits copied", bits_to_read, bits_copied);
		throw BitReaderError(BitReaderError::Code::FailedToReadBits, "Failed to read bits");
	}

	std::shared_ptr<const Data> BitReader::ReadBytes(const size_t buffer_length_in_bytes)
	{
		std::shared_ptr<const Data> buffer;

		if (_offset_of_current_byte == 0)
		{
			buffer = _data->Subdata(GetByteOffset(), buffer_length_in_bytes);
			SkipBits(buffer_length_in_bytes * 8);
		}
		else
		{
			// The data is not aligned to byte boundary
			// So read the data in bits
#if DEBUG
			logtw("Buffer is not aligned to byte boundary. Reading in bits.");
#endif	// DEBUG

			auto new_buffer = std::make_shared<Data>(buffer_length_in_bytes);
			new_buffer->SetLength(buffer_length_in_bytes);
			ReadBitsInternal(new_buffer->GetWritableData(), buffer_length_in_bytes, buffer_length_in_bytes * 8);
			buffer = new_buffer;
		}

		return buffer;
	}
}  // namespace ov
