//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2025 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "./assert.h"
#include "./byte_ordering.h"
#include "./data.h"
#include "./error.h"
#include "./memory_utilities.h"
#include "./type.h"

// Declare APIs to read various types such as:
//
// - `ReadAsU<bits><suffix>(const size_t bits_to_read)`
// - `ReadAsS<bits><suffix>(const size_t bits_to_read)`
// - `ReadU<bits><suffix>()`
// - `ReadS<bits><suffix>()`
// - `ReadU<bits><suffix>As()`
// - `ReadS<bits><suffix>As()`
//
// Usage:
//   OV_DEFINE_BITREADER_READ_APIS(<bits>, <endianness>, <unsigned_to_signed_converter>)
//
// Example:
//   OV_DEFINE_BITREADER_READ_APIS(24, LE, )
//
//   This will generate the following APIs:
//   - `ReadAsU24LE(const size_t bits_to_read)`
//   - `ReadAsS24LE(const size_t bits_to_read)`
//   - `ReadU24LE()`
//   - `ReadS24LE()`
//   - `ReadU24LEAs<T>()`
//   - `ReadS24LEAs<T>()`
#define OV_DEFINE_BITREADER_READ_APIS(bits, endianness, unsigned_to_signed_converter)                             \
	/* ReadAsU<bits><endianness>(const size_t bits_to_read) */                                                    \
	MAY_THROWS(BitReaderError)                                                                                    \
	inline auto ReadAsU##bits##endianness(const size_t bits_to_read)                                              \
	{                                                                                                             \
		return Read##endianness##As<uint##bits##_t>(bits_to_read);                                                \
	}                                                                                                             \
                                                                                                                  \
	/* ReadAsS<bits><endianness>(const size_t bits_to_read) */                                                    \
	MAY_THROWS(BitReaderError)                                                                                    \
	inline auto ReadAsS##bits##endianness(const size_t bits_to_read)                                              \
	{                                                                                                             \
		return static_cast<int##bits##_t>(unsigned_to_signed_converter(ReadAsU##bits##endianness(bits_to_read))); \
	}                                                                                                             \
                                                                                                                  \
	/* ReadU<bits><endianness>() */                                                                               \
	MAY_THROWS(BitReaderError)                                                                                    \
	inline auto ReadU##bits##endianness()                                                                         \
	{                                                                                                             \
		return ReadAsU##bits##endianness(sizeof(uint##bits##_t) * 8);                                             \
	}                                                                                                             \
                                                                                                                  \
	/* ReadS<bits><endianness>() */                                                                               \
	MAY_THROWS(BitReaderError)                                                                                    \
	inline auto ReadS##bits##endianness()                                                                         \
	{                                                                                                             \
		return static_cast<int##bits##_t>(unsigned_to_signed_converter(ReadU##bits##endianness()));               \
	}                                                                                                             \
                                                                                                                  \
	/* ReadU<bits><endianness>As() */                                                                             \
	MAY_THROWS(BitReaderError)                                                                                    \
	template <typename T>                                                                                         \
	inline T ReadU##bits##endianness##As()                                                                        \
	{                                                                                                             \
		return static_cast<T>(ReadU##bits##endianness());                                                         \
	}                                                                                                             \
                                                                                                                  \
	/* ReadS<bits><endianness>As() */                                                                             \
	MAY_THROWS(BitReaderError)                                                                                    \
	template <typename T>                                                                                         \
	inline T ReadS##bits##endianness##As()                                                                        \
	{                                                                                                             \
		return static_cast<T>(ReadS##bits##endianness());                                                         \
	}

namespace ov
{
	class BitReaderError : public Error
	{
	public:
		enum class Code : int
		{
			NotEnoughBitsToRead,
			NotEnoughSpaceInBuffer,
			FailedToReadBits,
		};

	public:
		BitReaderError(Code code, const char *message)
			: Error("BitReader", ToUnderlyingType(code), message)
		{
		}

		template <typename... Args>
		BitReaderError(Code code, const char *format, Args... args)
			: Error("BitReader", ToUnderlyingType(code), String::FormatString(format, args...))
		{
		}
	};

	template <typename T>
	inline T BitReaderBEToHost(T value)
	{
		return BEToHost(value);
	}

	template <>
	inline uint8_t BitReaderBEToHost<uint8_t>(uint8_t value)
	{
		return value;
	}

	template <typename T>
	inline T BitReaderLEToHost(T value)
	{
		return LEToHost(value);
	}

	template <>
	inline uint8_t BitReaderLEToHost<uint8_t>(uint8_t value)
	{
		return value;
	}

	// When calling `Read*()` API, if there is no data of that bit, it will throw `BitReaderError`.
	// So it must be wrapped in try/catch.
	class BitReader
	{
	public:
		BitReader(const void *buffer, size_t length);
		BitReader(const std::shared_ptr<const Data> &data);

		bool HasBits(size_t bits) const noexcept
		{
			return (_bits_remaining >= bits);
		}

		bool IsEmpty() const noexcept
		{
			return (_bits_remaining == 0);
		}

		// Rewind the reader to the beginning of the buffer
		bool SkipBits(const size_t bits_to_skip);
		bool SkipBytes(const size_t bytes_to_skip);
		bool SetBitOffset(const size_t offset);
		bool SetByteOffset(const size_t offset);

		size_t GetRemainingBits() const noexcept
		{
			return _bits_remaining;
		}

		size_t GetRemainingBytes() const noexcept
		{
			return (_bits_remaining + 7) / 8;
		}

		// The number of bits read from the buffer
		size_t GetBitOffset() const noexcept
		{
			return _bits_read;
		}

		// The number of bytes read from the buffer
		size_t GetByteOffset() const noexcept
		{
			return (_bits_read + 7) / 8;
		}

		// Get the remaining data in the buffer
		std::shared_ptr<const Data> GetData() const noexcept
		{
			auto offset = GetByteOffset();
			return (offset == 0) ? _data : _data->Subdata(offset);
		}

		MAY_THROWS(BitReaderError)
		template <typename T>
		T ReadBEAs(const size_t bits_to_read)
		{
			T value{};
			ReadBitsInternal(&value, sizeof(T), bits_to_read);

			if constexpr (std::is_enum_v<T>)
			{
				return static_cast<T>(BitReaderBEToHost(static_cast<UnderylingType<T>>(value)));
			}
			else
			{
				return static_cast<T>(BitReaderBEToHost(value));
			}
		}

		MAY_THROWS(BitReaderError)
		template <typename T>
		T ReadLEAs(const size_t bits_to_read)
		{
			T value{};
			ReadBitsInternal(&value, sizeof(T), bits_to_read);

			if constexpr (std::is_enum_v<T>)
			{
				return static_cast<T>(BitReaderLEToHost(static_cast<UnderylingType<T>>(value)));
			}
			else
			{
				return static_cast<T>(BitReaderLEToHost(value));
			}
		}

		// This API is an alias of `ReadBEAs()`
		//
		// Because it expected to be filled from LSB, so it should be treated as reading big endian
		MAY_THROWS(BitReaderError)
		template <typename T>
		T ReadAs(const size_t bits_to_read)
		{
			return ReadBEAs<T>(bits_to_read);
		}

		MAY_THROWS(BitReaderError)
		void Read(uint8_t *buffer, const size_t buffer_length_in_bytes, const uint8_t bits_to_read)
		{
			ReadBitsInternal(buffer, buffer_length_in_bytes, bits_to_read);
		}

		MAY_THROWS(BitReaderError)
		void ReadBytes(uint8_t *buffer, const size_t buffer_length_in_bytes)
		{
			ReadBitsInternal(buffer, buffer_length_in_bytes, buffer_length_in_bytes * 8);
		}

		MAY_THROWS(BitReaderError)
		std::shared_ptr<const Data> ReadBytes(const size_t buffer_length_in_bytes);

		// Read a single bit
		MAY_THROWS(BitReaderError)
		inline bool ReadBit()
		{
			auto value = false;

			if (_bits_remaining > 0)
			{
				value = ((*_current) >> (7 - _offset_of_current_byte)) & 0b1;
			}

			SkipBits(1);

			return value;
		}

		// - ReadAsU8(const size_t bits_to_read)
		// - ReadAs8(const size_t bits_to_read)
		// - ReadU8()
		// - Read8()
		// - ReadU8As()
		// - Read8As()
		OV_DEFINE_BITREADER_READ_APIS(8, , )

		// - ReadAsU16(const size_t bits_to_read)
		// - ReadAs16(const size_t bits_to_read)
		// - ReadU16()
		// - Read16()
		// - ReadU16As()
		// - Read16As()
		OV_DEFINE_BITREADER_READ_APIS(16, , )
		// Declare APIs to read 16-bit data considering endianness
		OV_DEFINE_BITREADER_READ_APIS(16, LE, )
		OV_DEFINE_BITREADER_READ_APIS(16, BE, )

		// - ReadAsU24(const size_t bits_to_read)
		// - ReadAs24(const size_t bits_to_read)
		// - ReadU24()
		// - Read24()
		// - ReadU24As()
		// - Read24As()
		OV_DEFINE_BITREADER_READ_APIS(24, , )
		// Declare APIs to read 24-bit data considering endianness
		OV_DEFINE_BITREADER_READ_APIS(24, LE, UInt24ToInt24)
		OV_DEFINE_BITREADER_READ_APIS(24, BE, UInt24ToInt24)

		// - ReadAsU32(const size_t bits_to_read)
		// - ReadAs32(const size_t bits_to_read)
		// - ReadU32()
		// - Read32()
		// - ReadU32As()
		// - Read32As()
		OV_DEFINE_BITREADER_READ_APIS(32, , )
		// Declare APIs to read 32-bit data considering endianness
		OV_DEFINE_BITREADER_READ_APIS(32, LE, )
		OV_DEFINE_BITREADER_READ_APIS(32, BE, )

		// - ReadAsU64(const size_t bits_to_read)
		// - ReadAs64(const size_t bits_to_read)
		// - ReadU64()
		// - Read64()
		// - ReadU64As()
		// - Read64As()
		OV_DEFINE_BITREADER_READ_APIS(64, , )
		// Declare APIs to read 64-bit data considering endianness
		OV_DEFINE_BITREADER_READ_APIS(64, LE, )
		OV_DEFINE_BITREADER_READ_APIS(64, BE, )

	protected:
		MAY_THROWS(BitReaderError)
		void ReadBitsInternal(void *buffer, const size_t length_in_bytes, const size_t bits_to_read);

		// NOTE: Since this API doesn't initialize `buffer` to `0`,
		// So initialize `buffer` to `0` before this function is called if needed
		void ReadBitsInternal(void *buffer, const size_t length_in_bit, const uint8_t bit_offset, const size_t bits_to_read);

		const std::shared_ptr<const Data> _data = nullptr;

		// `_current` points to the current position in the buffer
		const uint8_t *_current = nullptr;
		size_t _bits_read = 0;
		// The offset of the bit to be read from `*_current` (Range: 0-7, MSB=0, LSB=7)
		uint8_t _offset_of_current_byte = 0;
		// The number of bits remaining in the buffer (Initial value: `_buffer_length * 8`)
		size_t _bits_remaining = 0;
	};
}  // namespace ov
