//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================
#include "./string.h"

#include <algorithm>
#include <cctype>
#include <cstdarg>

#include "./assert.h"
#include "./data.h"
#include "./memory_utilities.h"
#include "./regex.h"

namespace ov
{
	String::String(uint32_t capacity)
	{
		SetCapacity(capacity);
	}

	String::String(const char *string)
	{
		Append(string);
	}

	String::String(const char *string, size_t length)
	{
		Append(string, length);
	}

	String::String(const String &string)
		: String(string._buffer, string.GetLength())
	{
	}

	String::String(String &&string) noexcept
		: _buffer(string._buffer),

		  _length(string._length),
		  _capacity(string._capacity)
	{
		string._buffer = nullptr;

		string._length = 0;
		string._capacity = 0;
	}

	String::~String()
	{
		Release();
	}

	String::operator const char *() const noexcept
	{
		return CStr();
	}

	const char *String::CStr() const noexcept
	{
		if (_buffer == nullptr)
		{
			return "";
		}

		return _buffer;
	}

	char *String::GetBuffer() noexcept
	{
		// 수정 가능한 포인터이기 때문에, 문자열이 없는 상태에서 호출되면 안됨
		OV_ASSERT(_buffer != nullptr, "Use \"const char *CStr()\" instead");

		return _buffer;
	}

	String &String::operator=(const String &buffer) noexcept
	{
		if (this == &buffer)
		{
			// 자기 자신일 경우, 대입하지 않아도 됨
			return *this;
		}

		if (_buffer != nullptr)
		{
			// 기존 버퍼에 복사하기 위해, 길이를 0으로 만듦
			_length = 0;
			_buffer[0] = '\0';
		}

		Append(buffer._buffer);

		return *this;
	}

	String &String::operator=(const char *buffer) noexcept
	{
		if (_buffer != nullptr)
		{
			// 기존 버퍼에 복사하기 위해, 길이를 0으로 만듦
			_length = 0;
			_buffer[0] = '\0';
		}

		Append(buffer);

		return *this;
	}

	const String &String::operator+=(const char *buffer) noexcept
	{
		Append(buffer);

		return *this;
	}

	String String::operator+(const String &other) const noexcept
	{
		String instance(*this);

		instance.Append(other);

		return instance;
	}

	bool String::Prepend(char c)
	{
		if (Alloc(_length + 1) == false)
		{
			return false;
		}

		// 1바이트씩 뒤로 이동
		::memmove(_buffer + 1, _buffer, static_cast<size_t>(_length));

		_buffer[0] = c;
		_length++;

		_buffer[_length] = '\0';

		return true;
	}

	bool String::Prepend(const char *string)
	{
		if (string == nullptr)
		{
			return false;
		}

		return Prepend(string, ::strlen(string));
	}

	bool String::Prepend(const char *string, size_t length)
	{
		if (string == nullptr)
		{
			return false;
		}

		if (length == 0)
		{
			// 문자열을 추가하지 않음
			return true;
		}

		// 기존 데이터 + 새 데이터가 들어갈 만한 공간을 확보한 뒤
		if (Alloc(_length + length) == false)
		{
			return false;
		}

		// 기존 데이터 앞에 Prepend
		::memmove(_buffer + length, _buffer, static_cast<size_t>(_length));

		::memcpy(_buffer, string, static_cast<size_t>(length));
		_length += length;

		_buffer[_length] = '\0';

		return true;
	}

	bool String::Append(char c)
	{
		if (Alloc(_length + 1) == false)
		{
			return false;
		}

		_buffer[_length] = c;
		_length++;

		_buffer[_length] = '\0';

		return true;
	}

	bool String::Append(const char *string)
	{
		if (string == nullptr)
		{
			return false;
		}

		return Append(string, ::strlen(string));
	}

	bool String::Append(const char *string, size_t length)
	{
		if (string == nullptr)
		{
			return false;
		}

		if (length == 0)
		{
			// 문자열을 추가하지 않음
			return true;
		}

		// 기존 데이터 + 새 데이터가 들어갈 만한 공간을 확보한 뒤
		if (Alloc(_length + length) == false)
		{
			return false;
		}

		// 기존 데이터 뒤에 Append
		if (_buffer != nullptr)
		{
			::memcpy(_buffer + _length, string, sizeof(char) * length);
		}

		_length += length;

		if (_buffer != nullptr)
		{
			_buffer[_length] = '\0';
		}

		return true;
	}

	size_t String::AppendFormat(const char *format, ...)
	{
		va_list list;
		size_t appended_length;

		va_start(list, format);

		appended_length = AppendVFormat(format, list);

		va_end(list);

		return appended_length;
	}

	size_t String::AppendVFormat(const char *format, va_list list)
	{
		va_list list_for_count;

		va_copy(list_for_count, list);

		int result = ::vsnprintf(nullptr, 0, format, list_for_count);

		if (result < 0)
		{
			return 0L;
		}

		size_t length = static_cast<size_t>(result);

		if (Alloc(_length + length) == false)
		{
			return 0;
		}

		if (_buffer != nullptr)
		{
			::vsnprintf(_buffer + _length, length + 1, format, list);
			_length += length;
		}

		if (_buffer != nullptr)
		{
			_buffer[_length] = '\0';
		}

		return length;
	}

	size_t String::Format(const char *format, ...)
	{
		va_list list;

		va_start(list, format);

		VFormat(format, list);

		va_end(list);

		return _length;
	}

	size_t String::VFormat(const char *format, va_list list)
	{
		va_list list_for_count;

		// vsnprintf로 길이를 얻어오면 list가 가장 마지막 파라미터를 가리키게 되므로
		// 현재 상태를 미리 백업함
		va_copy(list_for_count, list);

		int result = ::vsnprintf(nullptr, 0, format, list);

		if (result <= 0L)
		{
			return 0L;
		}

		size_t length = static_cast<size_t>(result);

		if (Alloc(length))
		{
			va_copy(list, list_for_count);

			::vsnprintf(_buffer, length + 1, format, list);
			_length = length;
		}

		return _length;
	}

	String String::FormatString(const char *format, ...)
	{
		va_list list;
		String buffer;

		va_start(list, format);

		buffer.VFormat(format, list);

		va_end(list);

		return buffer;
	}

	off_t String::IndexOf(char c, off_t start_position) const noexcept
	{
		if (start_position < 0)
		{
			return -1L;
		}

		for (auto index = static_cast<size_t>(start_position); index < _length; index++)
		{
			if (_buffer[index] == c)
			{
				return index;
			}
		}

		return -1L;
	}

	off_t String::IndexOf(const char *str, off_t start_position) const noexcept
	{
		if (start_position >= static_cast<off_t>(_length))
		{
			return -1L;
		}

		char *p = ::strstr(_buffer + start_position, str);

		if (p == nullptr)
		{
			return -1L;
		}

		return p - _buffer;
	}

	off_t String::IndexOfRev(char c, off_t start_position) const noexcept
	{
		off_t index;
		off_t start;

		if ((_length == 0L) || (start_position < -1L) || (start_position >= static_cast<off_t>(_length)))
		{
			return -1L;
		}

		if (start_position == -1L)
		{
			start = _length - 1L;
		}
		else
		{
			start = std::min(static_cast<off_t>(_length) - 1L, start_position);
		}

		for (index = start; index >= 0L; index--)
		{
			if (_buffer[index] == c)
			{
				return index;
			}
		}

		return -1L;
	}

	void String::PadLeft(size_t length, char pad)
	{
		size_t remained = length - GetLength();

		if (remained > 0L)
		{
			String padding;

			for (size_t index = 0; index < remained; index++)
			{
				padding.Append(pad);
			}

			Prepend(padding);
		}
	}

	void String::PadRight(size_t length, char pad)
	{
		if (length <= GetLength())
		{
			return;
		}

		size_t remained = length - GetLength();

		String padding;

		for (size_t index = 0L; index < remained; index++)
		{
			padding.Append(pad);
		}

		Append(padding);
	}

	void String::MakeUpper()
	{
		for (size_t index = 0L; index < _length; index++)
		{
			if (isalpha(_buffer[index]) != 0)
			{
				_buffer[index] = static_cast<char>(::toupper(_buffer[index]));
			}
		}
	}

	void String::MakeLower()
	{
		size_t index;

		for (index = 0L; index < _length; index++)
		{
			if (isalpha(_buffer[index]) != 0)
			{
				_buffer[index] = static_cast<char>(::tolower(_buffer[index]));
			}
		}
	}

	String String::Replace(const char *old_token, const char *new_token) const
	{
		String target;

		if (old_token == nullptr || new_token == nullptr)
		{
			return "";
		}

		off_t pos = 0L;
		size_t old_len = ::strlen(old_token);
		size_t new_len = ::strlen(new_token);
		const char *target_buffer = _buffer;

		while (true)
		{
			off_t new_pos = IndexOf(old_token, pos);

			if (new_pos == -1)
			{
				target.Append(target_buffer);
				break;
			}

			OV_ASSERT2(new_pos >= pos);

			target.Append(target_buffer, static_cast<size_t>(new_pos - pos));
			target.Append(new_token, new_len);

			target_buffer = _buffer + new_pos + old_len;
			pos = new_pos + old_len;
		}

		return target;
	}

	String String::Replace(const ov::Regex &regex, const char *new_token, bool replace_all) const
	{
		return regex.Replace(*this, new_token, replace_all, nullptr);
	}

	String String::Substring(off_t start) const
	{
		if ((start < 0L) || (_length < static_cast<size_t>(start)))
		{
			return "";
		}

		return Substring(start, _length - start);
	}

	String String::Substring(off_t start, size_t length) const
	{
		if ((start < 0L) || (_length < static_cast<size_t>(start)))
		{
			return "";
		}

		if ((start + length) > _length)
		{
			// _length 값보다 크면 보정
			length = _length - start;
		}

		return String(_buffer + start, length);
	}

	String String::Trim() const
	{
		size_t left_index;
		size_t right_index;

		if (_length == 0)
		{
			return "";
		}

		// 왼쪽 공백 위치 찾음
		for (left_index = 0L; left_index < _length; left_index++)
		{
			switch (_buffer[left_index])
			{
				case '\r':
				case '\n':
				case '\t':
				case ' ':
					continue;

				default:
					break;
			}

			break;
		}

		for (right_index = (_length - 1L); right_index >= left_index; right_index--)
		{
			switch (_buffer[right_index])
			{
				case '\r':
				case '\n':
				case '\t':
				case ' ':
					continue;

				default:
					break;
			}

			break;
		}

		if ((left_index == 0) && (right_index == (_length - 1L)))
		{
			// trim 할 게 없음
			return *this;
		}

		if (right_index < left_index)
		{
			// 공백으로만 이루어진 문자열
			return "";
		}

		// left_index는 공백이 아닌 문자 지점이므로
		return Substring(left_index, right_index - left_index + 1L);
	}

	String String::PadLeftString(size_t length, char pad) const
	{
		String padding = *this;

		padding.PadLeft(length, pad);

		return padding;
	}

	String String::PadRightString(size_t length, char pad) const
	{
		String padding = *this;

		padding.PadRight(length, pad);

		return padding;
	}

	String String::UpperCaseString() const
	{
		String str_upper = *this;

		str_upper.MakeUpper();

		return str_upper;
	}

	String String::LowerCaseString() const
	{
		String lower = *this;

		lower.MakeLower();

		return lower;
	}

	std::vector<String> String::Split(const char *string, size_t string_length, const char *separator, size_t max_count)
	{
		std::vector<String> list;
		const char *last;
		size_t separator_length;

		if (separator == nullptr)
		{
			if (string != nullptr)
			{
				list.emplace_back(string);
			}

			return list;
		}

		separator_length = ::strlen(separator);

		if (((string == nullptr) || (string_length == 0L)) || (separator_length == 0L))
		{
			if (string != nullptr)
			{
				list.emplace_back(string);
			}

			return list;
		}

		size_t token_count = 0;
		max_count = std::max(max_count, 1UL);

		while (token_count < max_count)
		{
			last = ::strstr(string, separator);

			auto length = ((last == nullptr) || (token_count == (max_count - 1))) ? (string_length * sizeof(char)) : ((last - string) * sizeof(char));

			list.emplace_back(string, length);

			if (last == nullptr)
			{
				break;
			}

			string_length -= (last - string) + separator_length;
			string = last + separator_length;

			token_count++;
		}

		return list;
	}

	std::vector<String> String::Split(const char *string, const char *separator, size_t max_count)
	{
		return Split(string, ::strlen(string), separator, max_count);
	}

	std::vector<String> String::Split(const char *separator, size_t max_count) const
	{
		return Split(CStr(), GetLength(), separator, max_count);
	}

	String String::Join(const std::vector<String> &list, const char *separator)
	{
		String string;
		bool is_first = true;

		for (auto const &item : list)
		{
			if (is_first == false)
			{
				string.Append(separator);
			}
			else
			{
				is_first = false;
			}

			string.Append(item.CStr());
		}

		return string;
	}

	String String::Join(const std::vector<String> &list, char separator)
	{
		String string;
		bool is_first = true;

		for (auto const &item : list)
		{
			if (is_first == false)
			{
				string.Append(separator);
			}
			else
			{
				is_first = false;
			}

			string.Append(item.CStr());
		}

		return string;
	}

	bool String::HasPrefix(String prefix) const
	{
		return (Left(prefix.GetLength()) == prefix);
	}

	bool String::HasPrefix(char prefix) const
	{
		return (Get(0) == prefix);
	}

	bool String::HasSuffix(String suffix) const
	{
		return (Right(suffix.GetLength()) == suffix);
	}

	bool String::HasSuffix(char suffix) const
	{
		return (Get(GetLength() - 1) == suffix);
	}

	String String::Left(size_t length) const
	{
		length = std::min(length, _length);

		return String(_buffer, length);
	}

	String String::Right(size_t length) const
	{
		size_t start_position;

		length = std::min(length, _length);
		start_position = _length - length;

		return String(_buffer + start_position, length);
	}

	char String::Get(off_t index) const
	{
		if ((index < 0) || (index >= static_cast<off_t>(_length)))
		{
			return 0;
		}

		return _buffer[index];
	}

	char String::operator[](off_t index) const
	{
		return Get(index);
	}

	bool String::operator!=(const char *buffer) const
	{
		return ((operator==(buffer)) == true) ? false : true;
	}

	bool String::IsEqualsTo(const char *str, size_t length) const
	{
		if (str == _buffer)
		{
			return true;
		}

		// Because we compare the two values above to see if they are the same, none of them are nullptr here.
		if (
			((str == nullptr) || (_buffer == nullptr)) &&
			(length != _length))
		{
			return false;
		}

		if (length != _length)
		{
			return false;
		}

		if (::strncmp(_buffer, str, _length) == 0)
		{
			return true;
		}

		return false;
	}

	bool String::operator==(const String &str) const
	{
		return IsEqualsTo(str.CStr(), str.GetLength());
	}

	bool String::operator==(const char *buffer) const
	{
		return IsEqualsTo(buffer, (buffer == nullptr) ? 0 : ::strlen(buffer));
	}

	bool String::operator<(const String &str) const
	{
		if (_buffer == nullptr)
		{
			if (str._buffer != nullptr)
			{
				// _buffer == nullptr && str != nullptr
				return true;
			}

			// _buffer == nullptr && str == nullptr
			return false;
		}

		if (str._buffer == nullptr)
		{
			// _buffer != nullptr && str == nullptr
			return false;
		}

		// _buffer != nullptr && str != nullptr
		if (::strcmp(_buffer, str._buffer) < 0)
		{
			return true;
		}

		return false;
	}

	bool String::operator>(const String &str) const
	{
		if (_buffer == nullptr)
		{
			// _buffer == nullptr && str != nullptr
			// _buffer == nullptr && str == nullptr
			return false;
		}

		if (str._buffer == nullptr)
		{
			// _buffer != nullptr && str == null
			return true;
		}

		// _buffer != nullptr && str != nullptr
		if (::strcmp(_buffer, str._buffer) > 0)
		{
			return true;
		}

		return false;
	}

	size_t String::GetCapacity() const noexcept
	{
		return _capacity;
	}

	bool String::SetCapacity(size_t length) noexcept
	{
		return Alloc(length, true);
	}

	size_t String::GetLength() const noexcept
	{
		return _length;
	}

	bool String::SetLength(size_t length) noexcept
	{
		if (length < _length)
		{
			// 버퍼 축소
			::memset(_buffer + length, 0, static_cast<size_t>(_length - length));
			_length = length;

			return true;
		}

		// 버퍼 확장
		if (Alloc(length))
		{
			// 길이 정보 저장
			_length = length;
			return true;
		}

		return false;
	}

	bool String::Clear()
	{
		return Release();
	}

	bool String::IsEmpty() const noexcept
	{
		return (_length == 0L);
	}

	bool String::IsNumeric() const noexcept
	{
		if (_buffer == nullptr)
		{
			return false;
		}

		for (size_t i = 0; i < _length; i++)
		{
			if (::isdigit(_buffer[i]) == 0)
			{
				return false;
			}
		}

		return true;
	}

	std::shared_ptr<Data> String::ToData(bool include_null_char) const
	{
		if (_buffer == nullptr)
		{
			return std::make_shared<ov::Data>();
		}

		return std::make_shared<ov::Data>(_buffer, _length + (include_null_char ? 1 : 0), false);
	}

	String String::Repeat(const char *str, size_t count)
	{
		if (count > 0)
		{
			auto length = ::strlen(str);
			auto total_length = (length * count);
			ov::String string(total_length);

			string.SetLength(total_length);
			auto buffer = string.GetBuffer();

			for (size_t index = 0; index < count; index++)
			{
				::memcpy(buffer, str, length);
				buffer += length;
			}

			return string;
		}

		return "";
	}

	bool String::Alloc(size_t length, bool alloc_exactly) noexcept
	{
		if (
			// 기존에 할당된 버퍼가 충분 하지 않거나
			(_capacity < length) ||
			// 기존에 할당된 버퍼가 너무 크거나
			//(_capacity > (length * 2)) ||
			// alloc_exactly flag가 켜져 있으면서 capacity가 정확히 일치하지 않는 경우
			(alloc_exactly && (_capacity != length)))
		{
			size_t allocated_length = length;

			if (alloc_exactly == false)
			{
				allocated_length = 1L;

				// 2의 거듭 제곱 크기로 계산
				while (allocated_length < length)
				{
					if (allocated_length >= 1024L * 1024L)
					{
						// 1MB를 넘어가면 지수 형태로 증가 하지 않음
						allocated_length += 1024L * 1024L;
					}
					else
					{
						// 1MB 미만일 경우 지수 형태로 증가
						allocated_length *= 2L;
					}
				}
			}

			// null문자가 들어갈 공간 더함
			char *buffer = static_cast<char *>(::malloc(sizeof(char) * (allocated_length + 1L)));

			if (buffer == nullptr)
			{
				// 메모리 할당 실패
				return false;
			}

			::memset(buffer, 0, sizeof(char) * (allocated_length + 1L));

			// 기존에 데이터가 있다면 복사
			if (_length > 0L)
			{
				::memcpy(buffer, _buffer, sizeof(char) * std::min(allocated_length, _length));
			}

			// 기존 버퍼 해제
			size_t old_length = _length;

			Release();

			_capacity = allocated_length;

			// 버퍼 대입
			_buffer = buffer;

			// Release()에 의해 길이 정보가 초기화 되었으므로 다시 대입해줌
			_length = old_length;
		}

		return true;
	}

	bool String::Release() noexcept
	{
		OV_SAFE_FREE(_buffer);

		_capacity = 0L;
		_length = 0L;

		return true;
	}
}  // namespace ov
