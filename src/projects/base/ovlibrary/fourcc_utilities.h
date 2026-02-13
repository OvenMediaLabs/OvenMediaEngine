#pragma once

#include "./string.h"

typedef uint32_t fourcc_t;

#if __cplusplus
namespace ov
{
	// This function creates a fourcc_t value from a 4-character string
	constexpr inline fourcc_t MakeFourCc(const char str[4])
	{
		return static_cast<fourcc_t>((static_cast<uint32_t>(str[0]) << 24) |
									 (static_cast<uint32_t>(str[1]) << 16) |
									 (static_cast<uint32_t>(str[2]) << 8) |
									 (static_cast<uint32_t>(str[3])));
	}

	// This function returns a string representation of the fourcc_t value
	inline const char *FourCcToString(fourcc_t fourcc)
	{
		thread_local char str[5]{0};

		str[0] = (fourcc >> 24) & 0xFF;
		str[0] = ::isalnum(static_cast<uint8_t>(str[0])) ? str[0] : ' ';
		str[1] = (fourcc >> 16) & 0xFF;
		str[1] = ::isalnum(static_cast<uint8_t>(str[1])) ? str[1] : ' ';
		str[2] = (fourcc >> 8) & 0xFF;
		str[2] = ::isalnum(static_cast<uint8_t>(str[2])) ? str[2] : ' ';
		str[3] = (fourcc) & 0xFF;
		str[3] = ::isalnum(static_cast<uint8_t>(str[3])) ? str[3] : ' ';

		return str;
	}
}  // namespace ov
#endif	// __cplusplus
