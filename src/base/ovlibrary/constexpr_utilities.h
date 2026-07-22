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

			while (*p1 != '\0')
			{
				const char *start = p1;
				const char *p2 = needle;

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

				// Partial match failed: resume from start + 1
				p1 = start + 1;
			}

			return nullptr;
		}

		constexpr char *StrStr(char *haystack, const char *needle)
		{
			// This cast is valid because haystack is a non-const pointer
			return const_cast<char *>(StrStr(static_cast<const char *>(haystack), needle));
		}
		static_assert(StrCmp(StrStr("Sample", "le"), "le") == 0, "StrStr() doesn't work properly");
		static_assert(StrCmp(StrStr("Sample", "Sa"), "Sample") == 0, "StrStr() doesn't work properly");
		static_assert(StrCmp(StrStr("ababac", "abac"), "abac") == 0, "StrStr() doesn't work properly");
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

		// A fixed-capacity, null-terminated character buffer usable in a constexpr context.
		// `Capacity` counts the terminating null, so it matches the size of the source string literal.
		template <size_t Capacity>
		struct FixedString
		{
			char value[Capacity] = {};

			constexpr const char *CStr() const
			{
				return value;
			}

			constexpr operator const char *() const
			{
				return value;
			}
		};

		// Returns `input` with every occurrence of `ch` removed, evaluated at compile time.
		// The result keeps the source capacity, so removed characters leave zero-filled trailing bytes
		// after the null terminator; read as a C string it yields the shortened text.
		template <size_t Capacity>
		constexpr FixedString<Capacity> StrRemove(const char (&input)[Capacity], char ch)
		{
			FixedString<Capacity> result{};
			size_t out = 0;

			for (size_t in = 0; in < Capacity; ++in)
			{
				if (input[in] != ch)
				{
					result.value[out++] = input[in];
				}
			}

			return result;
		}
		static_assert(StrCmp(StrRemove("a-b-c", '-').CStr(), "abc") == 0, "StrRemove() doesn't work properly");
		static_assert(StrCmp(StrRemove("nodash", '-').CStr(), "nodash") == 0, "StrRemove() doesn't work properly");
		static_assert(StrCmp(StrRemove("ab-", '-').CStr(), "ab") == 0, "StrRemove() doesn't work properly");
		static_assert(StrCmp(StrRemove("---", '-').CStr(), "") == 0, "StrRemove() doesn't work properly");
		static_assert(StrCmp(StrRemove("", '-').CStr(), "") == 0, "StrRemove() doesn't work properly");
	}  // namespace cexpr
}  // namespace ov
