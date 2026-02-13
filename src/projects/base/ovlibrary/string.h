//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <memory.h>

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "./format_string.h"

namespace ov
{
	class Data;
	class Regex;

	class String
	{
	public:
		String() = default;
		String(const char *string);	 // NOLINT
		String(const char *string, size_t length);
		String(uint32_t capacity);

		// copy constructor
		String(const String &str);

		// move constructor
		String(String &&str) noexcept;

		~String();

		operator const char *() const noexcept;	 // NOLINT
		const char *CStr() const noexcept;
		// Returns a "modifiable" buffer.
		// You must obtain and use this buffer only when you call SetLength() or if you know the length exactly.
		char *GetBuffer() noexcept;

		// String manipulation API
		String &operator=(const String &buffer) noexcept;
		String &operator=(const char *buffer) noexcept;
		const String &operator+=(const char *buffer) noexcept;
		String operator+(const String &other) const noexcept;

		// Adds a character or string before current string
		bool Prepend(char c);
		bool Prepend(const char *string);
		bool Prepend(const char *string, size_t length);

		// Adds a character or string after current string
		bool Append(char c);
		bool Append(const char *string);
		bool Append(const char *string, size_t length);

		// Adds a string created by the format after current string
		size_t AppendFormat(const char *format, ...) __attribute__((format(printf, 2, 3)));
		size_t AppendVFormat(const char *format, va_list list);

		// Sets a string created by the format
		size_t Format(const char *format, ...) OV_PRINTF_FORMAT(2, 3);
		size_t VFormat(const char *format, va_list list);

		// Creates new instance
		static String FormatString(const char *format, ...) OV_PRINTF_FORMAT(1, 2);

		// Returns the position from start_position to end of the string, where the letter appears.
		off_t IndexOf(char c, off_t start_position = 0) const noexcept;
		off_t IndexOf(const char *str, off_t start_position = 0) const noexcept;
		// Returns the position from start_position to start of the string, where the letter appears.
		off_t IndexOfRev(char c, off_t start_position = -1) const noexcept;

		// In-place utilities
		void PadLeft(size_t length, char pad = ' ');
		void PadRight(size_t length, char pad = ' ');
		void MakeUpper();
		void MakeLower();

		// Out-of-place utilities
		String Replace(const char *old_token, const char *new_token) const;
		String Replace(const ov::Regex &regex, const char *new_token, bool replace_all = false) const;
		String Substring(off_t start) const;
		String Substring(off_t start, size_t length) const;
		String Trim() const;

		String PadLeftString(size_t length, char pad = ' ') const;
		String PadRightString(size_t length, char pad = ' ') const;
		String UpperCaseString() const;
		String LowerCaseString() const;

		static std::vector<String> Split(const char *string, size_t string_length, const char *separator, size_t max_count = SIZE_MAX);
		static std::vector<String> Split(const char *string, const char *separator, size_t max_count = SIZE_MAX);
		std::vector<String> Split(const char *separator, size_t max_count = SIZE_MAX) const;

		static String Join(const std::vector<String> &list, const char *separator);
		static String Join(const std::vector<String> &list, char separator);

		bool HasPrefix(String prefix) const;
		bool HasPrefix(char prefix) const;
		bool HasSuffix(String suffix) const;
		bool HasSuffix(char suffix) const;

		String Left(size_t length) const;
		String Right(size_t length) const;

		// Obtains a character at index
		char Get(off_t index) const;
		char operator[](off_t index) const;

		bool operator!=(const char *buffer) const;
		bool operator==(const String &str) const;
		bool operator==(const char *buffer) const;
		bool operator<(const String &str) const;
		bool operator>(const String &str) const;

		// Obtains allocated memory size
		size_t GetCapacity() const noexcept;

		// Preallocate a memory to avoid frequent memory allocation
		bool SetCapacity(size_t length) noexcept;

		// Obtains current string length
		size_t GetLength() const noexcept;

		// Forced to adjust the length of string.
		// If the ```length``` is longer than the current length, fill the rest of the space with zero (therefore, it is usually meaningless).
		// If the ```length``` is shorter than the current length, crop the string.
		bool SetLength(size_t length) noexcept;

		// Initialize to a string of zero length.
		bool Clear();

		bool IsEmpty() const noexcept;
		bool IsNumeric() const noexcept;

		std::shared_ptr<Data> ToData(bool include_null_char = true) const;

		static ov::String Repeat(const char *str, size_t count);

		std::size_t Hash() const
		{
			auto temp = std::string_view(CStr(), GetLength());
			return std::hash<std::string_view>{}(temp);
		}

	protected:
		bool IsEqualsTo(const char *str, size_t length) const;

		bool Alloc(size_t length, bool alloc_exactly = false) noexcept;
		bool Release() noexcept;

	private:
		char *_buffer = nullptr;

		// Actual string length
		size_t _length = 0;

		// Allocated memory size (Exponentially increasing)
		size_t _capacity = 0;
	};

	struct CaseInsensitiveHash
	{
		std::size_t operator()(const String &str) const
		{
			return str.LowerCaseString().Hash();
		}
	};

	struct CaseInsensitiveEqual
	{
		bool operator()(const String &lhs, const String &rhs) const
		{
			return lhs.LowerCaseString() == rhs.LowerCaseString();
		}
	};

	struct CaseInsensitiveComparator
	{
		// for std::map
		bool operator()(const String &s1, const String &s2) const
		{
			return s1.UpperCaseString() < s2.UpperCaseString();
		}
	};
}  // namespace ov

namespace std
{
	template <>
	struct hash<ov::String>
	{
		std::size_t operator()(ov::String const &str) const
		{
			return str.Hash();
		}
	};
}  // namespace std
