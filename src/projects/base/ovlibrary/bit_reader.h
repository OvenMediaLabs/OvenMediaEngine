#pragma once

#include <base/ovlibrary/ovlibrary.h>
#include <stdint.h>

#include <algorithm>
#include <type_traits>
#include <utility>

#include "byte_io.h"

class BitReader
{
public:
	BitReader(const uint8_t *buffer, size_t capacity);
	BitReader(const std::shared_ptr<const ov::Data> &data);

	template <typename T>
	T ReadBytes(bool big_endian = true)
	{
		T value;

		if (ReadBytes(value, big_endian) == false)
		{
			return 0;
		}

		return value;
	}

	bool SkipAll()
	{
		return SkipBytes(BytesRemained());
	}

	bool SkipBytes(size_t length)
	{
		if (length > static_cast<size_t>(_capacity - (_position - _buffer)))
		{
			return false;
		}

		_position += length;

		return true;
	}

	ov::String ReadString(size_t length)
	{
		length = std::min(length, BytesRemained());

		ov::String str(reinterpret_cast<const char *>(_position), length);

		_position += length;

		return str;
	}

	// Note: ReadBytes() API obtains the bits without considering _bit_offset
	template <typename T>
	bool ReadBytes(T &value, bool big_endian = true)
	{
		if (sizeof(value) > static_cast<size_t>(_capacity - (_position - _buffer)))
		{
			return false;
		}

		value = big_endian ? ByteReader<T>::ReadBigEndian(_position) : ByteReader<T>::ReadLittleEndian(_position);

		_position += sizeof(value);

		return true;
	}

	template <typename T>
	T ReadBits(uint8_t bits)
	{
		T value;

		if (ReadBits<T>(bits, value))
		{
			return value;
		}

		return static_cast<T>(0);
	}

	template <typename T>
	bool ReadBits(uint8_t bits, T &value)
	{
		if constexpr (std::is_enum_v<T>)
		{
			auto underlying_value = static_cast<std::underlying_type_t<T>>(value);
			if (ReadBitsInternal(bits, underlying_value))
			{
				value = static_cast<T>(underlying_value);
				return true;
			}

			return false;
		}
		else
		{
			return ReadBitsInternal(bits, value);
		}
	}

	bool ReadBit(uint8_t &value)
	{
		return ReadBits<uint8_t>(1, value);
	}

	bool ReadBoolBit()
	{
		bool value;
		bool result = ReadBit(value);
		if (result == false)
		{
			return false;
		}

		return value;
	}

	bool ReadBit(bool &value)
	{
		uint8_t bit;
		if (ReadBit(bit))
		{
			value = bit == 1 ? true : false;
			return true;
		}
		return false;
	}

	uint8_t ReadBit()
	{
		uint8_t value;
		bool result = ReadBit(value);
		if (result == false)
		{
			return 0;
		}

		return value;
	}

	void StartSection()
	{
		_lap_position = _position;
	}

	size_t BytesSetionConsumed()
	{
		if (_lap_position == nullptr)
		{
			return 0;
		}

		auto bytes = _position - _lap_position;
		return bytes;
	}

	const uint8_t *CurrentPosition()
	{
		return _position;
	}

	size_t BytesRemained() const
	{
		return _capacity - BytesConsumed();
	}

	size_t BitsRemained() const
	{
		return (_capacity * 8) - BitsConsumed();
	}

	size_t BytesConsumed() const
	{
		return _position - _buffer;
	}

	size_t BitsConsumed() const
	{
		return (BytesConsumed() * 8) + _bit_offset;
	}

protected:
	template <typename T>
	bool ReadBitsInternal(uint8_t bits, T &value)
	{
		if (bits > sizeof(value) * 8)
		{
			OV_ASSERT2(false);
			return false;
		}

		value = 0;

		if (bits == 0)
		{
			return true;
		}

		if (static_cast<size_t>((bits + 7) / 8) > static_cast<size_t>(_capacity - (_position - _buffer)))
		{
			return false;
		}

		while (bits)
		{
			const uint8_t bits_from_this_byte = std::min(bits >= 8 ? 8 : bits % 8, 8 - _bit_offset);
			const uint8_t mask_offset = 8 - bits_from_this_byte - _bit_offset;
			const uint8_t mask = ((1 << bits_from_this_byte) - 1) << mask_offset;
			value <<= bits_from_this_byte;
			value |= (*_position & mask) >> mask_offset;
			bits -= bits_from_this_byte;
			_bit_offset += bits_from_this_byte;
			if (_bit_offset == 8)
			{
				NextPosition();
				_bit_offset = 0;
			}
		}

		return true;
	}

	virtual void NextPosition()
	{
		_position++;
	}

	const uint8_t *_buffer;
	const uint8_t *_position;
	const uint8_t *_lap_position;
	size_t _capacity;
	int _bit_offset = 0;
	uint8_t _mask = 0x80;
};
