//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#if defined(__APPLE__)
#	include <libkern/OSByteOrder.h>
#	include <machine/endian.h>

#	define htole16(x) OSSwapHostToLittleInt16(x)
#	define htole32(x) OSSwapHostToLittleInt32(x)
#	define htole64(x) OSSwapHostToLittleInt64(x)
#	define le16toh(x) OSSwapLittleToHostConstInt16(x)
#	define le32toh(x) OSSwapLittleToHostConstInt32(x)
#	define le64toh(x) OSSwapLittleToHostConstInt64(x)
#	define htobe16(x) OSSwapHostToBigInt16(x)
#	define htobe32(x) OSSwapHostToBigInt32(x)
#	define htobe64(x) OSSwapHostToBigInt64(x)
#	define be16toh(x) OSSwapBigToHostConstInt16(x)
#	define be32toh(x) OSSwapBigToHostConstInt32(x)
#	define be64toh(x) OSSwapBigToHostConstInt64(x)
#else
#	include <arpa/inet.h>
#	include <endian.h>
#endif
#include <cinttypes>

#if (__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)
#	define OV_IS_LITTLE_ENDIAN 1
#elif (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
#	define OV_IS_LITTLE_ENDIAN 0
#else
#	error Could not identify byte ordering
#endif

#include "./memory_utilities.h"

#pragma pack(push, 1)
struct uint24_t
{
	uint24_t(uint32_t value) noexcept
		: data(value)
	{
	}

	uint24_t()
	{
		data = 0U;
	}

	uint24_t(const uint24_t &value) noexcept = default;
	uint24_t(uint24_t &&value) noexcept = default;

	uint32_t data : 24;

	inline operator uint32_t() const noexcept
	{
		return data;
	}

	uint24_t &operator=(const uint24_t &value) noexcept
	{
		data = value.data;
		return *this;
	}
};

struct int24_t
{
	int24_t(int32_t value) noexcept
	{
		SetValue(value);
	}

	int24_t()
	{
		SetValue(0);
	}

	int24_t(const int24_t &value) noexcept = default;
	int24_t(int24_t &&value) noexcept = default;

	int32_t data;

	inline operator int32_t() const noexcept
	{
		return data;
	}

	int24_t &operator=(const int32_t &value) noexcept
	{
		SetValue(value);
		return *this;
	}

	int24_t &operator=(const int24_t &value) noexcept
	{
		SetValue(value.data);
		return *this;
	}

	int24_t &operator|=(const int24_t &value) noexcept
	{
		SetValue(data | value.data);
		return *this;
	}

private:
	void SetValue(int32_t value) noexcept
	{
		value &= MASK;

		if (value & SIGN_BIT)
		{
			value |= ~MASK;
		}

		data = value;
	}

private:
	static constexpr int32_t MASK = 0xFFFFFF;
	static constexpr int32_t SIGN_BIT = 0x800000;
};
#pragma pack(pop)

// OV_SELECT_BY_ENDIAN(call_when_little_endian, call_when_big_endian) 정의
#if OV_IS_LITTLE_ENDIAN
#	define OV_SELECT_BY_ENDIAN(call_when_little_endian, call_when_big_endian) \
		call_when_little_endian
#else  // OV_IS_LITTLE_ENDIAN
#	define OV_SELECT_BY_ENDIAN(call_when_little_endian, call_when_big_endian) \
		call_when_big_endian
#endif	// OV_IS_LITTLE_ENDIAN

#define OV_DECLARE_ENDIAN_FUNCTION(bits, name_prefix, name_suffix, func_prefix, func_suffix) \
	inline uint##bits##_t name_prefix##bits##name_suffix(uint##bits##_t value)               \
	{                                                                                        \
		return func_prefix##bits##func_suffix(value);                                        \
	}                                                                                        \
	inline uint##bits##_t name_prefix##name_suffix(uint##bits##_t value) noexcept            \
	{                                                                                        \
		return name_prefix##bits##name_suffix(value);                                        \
	}

#define OV_DECLARE_ENDIAN_FUNCTIONS(name_prefix, name_suffix, func_prefix, func_suffix) \
	OV_DECLARE_ENDIAN_FUNCTION(16, name_prefix, name_suffix, func_prefix, func_suffix)  \
	OV_DECLARE_ENDIAN_FUNCTION(24, name_prefix, name_suffix, func_prefix, func_suffix)  \
	OV_DECLARE_ENDIAN_FUNCTION(32, name_prefix, name_suffix, func_prefix, func_suffix)  \
	OV_DECLARE_ENDIAN_FUNCTION(64, name_prefix, name_suffix, func_prefix, func_suffix)

// The existing values defined in `endian.h` do not have type coercion, making them inconvenient to use.
// This is to solve that problem.
namespace ov
{
	inline uint24_t be24toh(uint24_t value) noexcept
	{
		return OV_SELECT_BY_ENDIAN(be32toh(value << 8), value);
	}

	inline uint24_t htobe24(uint24_t value) noexcept
	{
		return OV_SELECT_BY_ENDIAN(htobe32(value) >> 8, value);
	}

	inline uint24_t le24toh(uint24_t value) noexcept
	{
		return OV_SELECT_BY_ENDIAN(value, le32toh(value << 8));
	}

	inline uint24_t htole24(uint24_t value) noexcept
	{
		return OV_SELECT_BY_ENDIAN(value, htole32(value) >> 8);
	}

	inline int24_t UInt24ToInt24(const uint24_t &uint_value) noexcept
	{
		auto uint_data = uint_value.data;

		// Check the sign bit and set the sign bit accordingly
		return OV_CHECK_FLAG(uint_data, 1 << 23)
				   // The sign bit is set
				   ? static_cast<int32_t>(uint_data | 0xFF000000)
				   : static_cast<int32_t>(uint_data);
	}

	inline uint24_t Int24ToUInt24(const int24_t &int_value) noexcept
	{
		auto int_data = int_value.data;

		// Check the sign bit and set the sign bit accordingly
		return (int_data < 0)
				   // The sign bit is set
				   ? static_cast<uint32_t>((int_data & 0x7FFFFF) | (1 << 23))
				   : static_cast<uint32_t>(int_data);
	}

	inline bool IsLittleEndian() noexcept
	{
		return OV_SELECT_BY_ENDIAN(false, true);
	}

	inline bool IsBigEndian() noexcept
	{
		return OV_SELECT_BY_ENDIAN(false, true);
	}

	// APIs to change endianness
	//
	// Example)
	// uint16_t value = HostToLE16(0x00F0);
	// uint32_t value = HostToLE32(0x00FF00F0);

	// Host => Little Endian
	//
	// - HostToLE(uint16_t/uint24_t/uint32_t/uint64_t)
	// - HostToLE16(uint16_t)
	// - HostToLE24(uint24_t)
	// - HostToLE32(uint32_t)
	// - HostToLE64(uint64_t)
	OV_DECLARE_ENDIAN_FUNCTIONS(HostToLE, , htole, );

	// Little Endian => Host
	//
	// - LEToHost(uint16_t/uint24_t/uint32_t/uint64_t)
	// - LE16ToHost(uint16_t)
	// - LE24ToHost(uint24_t)
	// - LE32ToHost(uint32_t)
	// - LE64ToHost(uint64_t)
	OV_DECLARE_ENDIAN_FUNCTIONS(LE, ToHost, le, toh);

	// Host => Big Endian
	//
	// - HostToBE(uint16_t/uint24_t/uint32_t/uint64_t)
	// - HostToBE16(uint16_t)
	// - HostToBE24(uint24_t)
	// - HostToBE32(uint32_t)
	// - HostToBE64(uint64_t)
	OV_DECLARE_ENDIAN_FUNCTIONS(HostToBE, , htobe, );

	// Big Endian => Host
	//
	// - BEToHost(uint16_t/uint24_t/uint32_t/uint64_t)
	// - BE16ToHost(uint16_t)
	// - BE24ToHost(uint24_t)
	// - BE32ToHost(uint32_t)
	// - BE64ToHost(uint64_t)
	OV_DECLARE_ENDIAN_FUNCTIONS(BE, ToHost, be, toh);

	// Host => Network Endian (alias of Host => BE)
	//
	// - HostToNetwork(uint16_t/uint24_t/uint32_t/uint64_t)
	// - HostToNetwork16(uint16_t)
	// - HostToNetwork24(uint24_t)
	// - HostToNetwork32(uint32_t)
	// - HostToNetwork64(uint64_t)
	OV_DECLARE_ENDIAN_FUNCTIONS(HostToNetwork, , HostToBE, );

	// Network Endian => Host (alias of BE => Host)
	//
	// - NetworkToHost(uint16_t/uint24_t/uint32_t/uint64_t)
	// - NetworkToHost16(uint16_t)
	// - NetworkToHost24(uint24_t)
	// - NetworkToHost32(uint32_t)
	// - NetworkToHost64(uint64_t)
	OV_DECLARE_ENDIAN_FUNCTIONS(NetworkToHost, , BE, ToHost);

}  // namespace ov
