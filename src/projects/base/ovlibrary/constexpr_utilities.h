//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2025 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <sys/types.h>

#include <cstddef>

// Utilities to use in constexpr context
namespace ov
{
	namespace cexpr
	{
		constexpr size_t StrLen(const char *str)
		{
			size_t length = 0;

			while (*str++)
			{
				++length;
			}

			return length;
		}
		static_assert(StrLen("Sample") == 6, "StrLen() doesn't work properly");

		constexpr int StrCmp(const char *str1, const char *str2)
		{
			while (*str1 && (*str1 == *str2))
			{
				++str1;
				++str2;
			}

			if (*str1 == *str2)
			{
				return 0;
			}

			return (*str1 < *str2) ? -1 : 1;
		}
		static_assert(StrCmp("Sample", "Sample") == 0, "StrCmp() doesn't work properly");
		static_assert(StrCmp("Sample", "SAMPLE") == 1, "StrCmp() doesn't work properly");
		static_assert(StrCmp("SAMPLE", "Sample") == -1, "StrCmp() doesn't work properly");

		constexpr bool StrNCmp(const char *str1, const char *str2, size_t n)
		{
			while ((n > 0) && (*str1 != '\0') && (*str1 == *str2))
			{
				++str1;
				++str2;
				--n;
			}

			return (n == 0) || (*str1 == *str2);
		}
		static_assert(StrNCmp("Sample", "Sample", 6) == true, "StrNCmp() doesn't work properly");
		static_assert(StrNCmp("Sample", "SaMPLE", 2) == true, "StrNCmp() doesn't work properly");

		constexpr const char *StrStr(const char *haystack, const char *needle)
		{
			if (*needle == '\0')
			{
				// Empty needle matches at the start of haystack
				return haystack;
			}

			const char *p1 = haystack;
			const char *p2 = needle;

			while (*p1 != '\0')
			{
				if (*p1 == *p2)
				{
					const char *start = p1;

					while (*p2 != '\0' && *p1 == *p2)
					{
						++p1;
						++p2;
					}

					if (*p2 == '\0')
					{
						// Found the needle
						return start;
					}

					// Reset p2 to the start of needle
					p2 = needle;
				}

				++p1;
			}

			return nullptr;
		}

		constexpr char *StrStr(char *haystack, const char *needle)
		{
			// This cast is valid because haystack is a non-const pointer
			return const_cast<char *>(StrStr(haystack, needle));
		}
		static_assert(StrCmp(StrStr("Sample", "le"), "le") == 0, "StrStr() doesn't work properly");
		static_assert(StrCmp(StrStr("Sample", "Sa"), "Sample") == 0, "StrStr() doesn't work properly");
		static_assert(StrStr("Sample", "LE") == nullptr, "StrStr() doesn't work properly");

		constexpr off_t IndexOf(const char *str, const char *sub_str, off_t start_position = 0)
		{
			if (start_position >= static_cast<off_t>(StrLen(str)))
			{
				return -1L;
			}

			const char *p = StrStr(str + start_position, sub_str);

			if (p == nullptr)
			{
				return -1L;
			}

			return (p - str);
		}
		static_assert(IndexOf("Sample", "le", 0) == 4, "IndexOf() doesn't work properly");

		constexpr off_t IndexOf(const char *str, char sub_char, off_t start_position = 0)
		{
			if (start_position >= static_cast<off_t>(StrLen(str)))
			{
				return -1L;
			}

			const char *p = str + start_position;

			while (*p != '\0')
			{
				if (*p == sub_char)
				{
					return (p - str);
				}

				++p;
			}

			return -1L;
		}
		static_assert(IndexOf("Sample", 'm', 0) == 2, "IndexOf() doesn't work properly");
		static_assert(IndexOf("Sample", 'm', 4) < 0, "IndexOf() doesn't work properly");

		template <typename T, typename... Args>
		constexpr T Min(T a, T b, Args... args)
		{
			if constexpr (sizeof...(args) == 0)
			{
				return a < b ? a : b;
			}
			else
			{
				return Min(a < b ? a : b, args...);
			}
		}
		static_assert(Min(3, 1, 2) == 1, "Min() doesn't work properly");

		template <typename T, typename... Args>
		constexpr T Max(T a, T b, Args... args)
		{
			if constexpr (sizeof...(args) == 0)
			{
				return a > b ? a : b;
			}
			else
			{
				return Max(a > b ? a : b, args...);
			}
		}
		static_assert(Max(3, 1, 2) == 3, "Max() doesn't work properly");
	}  // namespace cexpr
}  // namespace ov
