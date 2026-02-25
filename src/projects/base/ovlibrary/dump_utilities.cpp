//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================
#include "./dump_utilities.h"

#include "./data.h"
#include "./ovlibrary_private.h"

#define __STDC_FORMAT_MACROS

#include <cxxabi.h>

#include <cctype>
#include <cinttypes>
#include <cmath>

namespace ov
{
	String Demangle(const char *func)
	{
		int status;
		char *demangled = abi::__cxa_demangle(func, nullptr, nullptr, &status);
		String result;

		if (demangled != nullptr)
		{
			result = demangled;
			::free(demangled);
		}
		else
		{
			result = func;
		}

		return result;
	}

	// Dump utilities
	// TODO(dimiden): Need to optimize this
	String ToHexStringWithDelimiter(const void *data, size_t length, char delimiter)
	{
		String dump;

		const auto *buffer = static_cast<const uint8_t *>(data);

		for (size_t index = 0; index < length; index++)
		{
			dump.AppendFormat("%02X", *buffer);

			if (index < length - 1)
			{
				dump.Append(delimiter);
			}
			buffer++;
		}

		return dump;
	}

	String ToHexStringWithDelimiter(const Data *data, char delimiter)
	{
		return ToHexStringWithDelimiter(data->GetData(), data->GetLength(), delimiter);
	}

	String ToHexString(const void *data, size_t length)
	{
		String dump;

		const auto *buffer = static_cast<const uint8_t *>(data);

		for (size_t index = 0; index < length; index++)
		{
			dump.AppendFormat("%02X", *buffer);
			buffer++;
		}

		return dump;
	}

	String ToUUIDString(const void *data, size_t length)
	{
		if (length != 16)
		{
			// Invalid UUID
			return "";
		}

		String dump;

		const auto *buffer = static_cast<const uint8_t *>(data);

		for (size_t index = 0; index < length; index++)
		{
			dump.AppendFormat("%02X", *buffer);

			if (index == 3 || index == 5 || index == 7 || index == 9)
			{
				dump.Append("-");
			}

			buffer++;
		}

		return dump;
	}

	String Dump(const void *data, size_t length, const char *title, off_t offset, size_t max_bytes, const char *line_prefix) noexcept
	{
		if (offset > static_cast<off_t>(length))
		{
			offset = length;
		}

		String dump;
		const char *buffer = ((const char *)data + offset);
		max_bytes = std::min(length - static_cast<size_t>(offset), max_bytes);

		// Dump up to 1MB
		int dump_bytes = (int)std::min(max_bytes, (1024UL * 1024UL));

		if (title == nullptr)
		{
			title = "Memory dump of";
		}

		if (line_prefix == nullptr)
		{
			line_prefix = "";
		}

		if (offset > 0)
		{
			// ========== xxxxx 0x12345678 + 0xABCDEF01 (102400 / 1024000) ==========
			dump.AppendFormat("%s========== %s 0x%08" PRIXPTR " + 0x%08" PRIXMAX " (%d/%zu bytes) ==========", line_prefix, title, reinterpret_cast<uintptr_t>(data), static_cast<uintmax_t>(offset), dump_bytes, length);
		}
		else
		{
			// ========== xxxxx 0x12345678 (102400 / 1024000) ==========
			dump.AppendFormat("%s========== %s 0x%08" PRIXPTR " (%d/%zu bytes) ==========", line_prefix, title, reinterpret_cast<uintptr_t>(data), dump_bytes, length);
		}

		if (dump_bytes == 0L)
		{
			dump.AppendFormat("\n%s(Data is empty)", line_prefix);
		}
		else
		{
			String ascii;
			int padding = (int)(log((double)dump_bytes) / log(16.0)) + 1;
			String number = String::FormatString("\n%s%%0%dX | ", line_prefix, padding);

			if (dump_bytes > 0)
			{
				dump.AppendFormat(number, 0L);
			}

			// 00 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F | 0123456789ABCDEF
			for (int index = 0; index < dump_bytes; index++)
			{
				if ((index > 0) && (index % 16 == 0))
				{
					dump.AppendFormat("| %s", ascii.CStr());

					if (index <= (dump_bytes - 1))
					{
						dump.AppendFormat(number, index);
					}

					ascii = "";
				}

				char character = buffer[index];

				if (!isprint(character))
				{
					character = '.';
				}

				ascii.Append(character);
				dump.AppendFormat("%02X ", (unsigned char)(buffer[index]));
			}

			if (ascii.IsEmpty() == false)
			{
				if (dump_bytes % 16 > 0)
				{
					String pad_string;

					pad_string.PadRight(static_cast<size_t>((16 - (dump_bytes % 16)) * 3));

					dump.Append(pad_string);
				}

				dump.AppendFormat("| %s", ascii.CStr());
			}
		}

		return dump;
	}

	String Dump(const void *data, size_t length, size_t max_bytes) noexcept
	{
		return Dump(data, length, nullptr, 0, max_bytes, nullptr);
	}

	std::shared_ptr<FILE> DumpToFile(const char *file_name, const void *data, size_t length, off_t offset, bool append) noexcept
	{
		if (offset < 0L)
		{
			return nullptr;
		}

		FILE *file = ::fopen(file_name, append ? "ab" : "wb");

		if (file == nullptr)
		{
			return nullptr;
		}

		::fwrite(static_cast<const uint8_t *>(data) + offset, sizeof(uint8_t), static_cast<size_t>(length), file);
		::fflush(file);

		return std::shared_ptr<FILE>(file, [](FILE *file) {
			if (file != nullptr)
			{
				::fclose(file);
			}
		});
	}

	std::shared_ptr<FILE> DumpToFile(const char *file_name, const std::shared_ptr<const Data> &data, off_t offset, bool append) noexcept
	{
		if (data == nullptr)
		{
			return nullptr;
		}

		return DumpToFile(file_name, data->GetData(), data->GetLength(), offset, append);
	}

	std::shared_ptr<Data> LoadFromFile(const char *file_name) noexcept
	{
		FILE *file = ::fopen(file_name, "rb");
		if (file == nullptr)
		{
			return nullptr;
		}

		if (::fseeko(file, 0L, SEEK_END) != 0)
		{
			::fclose(file);
			return nullptr;
		}

		auto length = ::ftello(file);
		if (length <= 0L)
		{
			::fclose(file);
			return nullptr;
		}
		
		if (::fseeko(file, 0L, SEEK_SET) != 0)
		{
			::fclose(file);
			return nullptr;
		}

		auto data = std::make_shared<Data>(length);
		data->SetLength(length);

		if (::fread(data->GetWritableData(), sizeof(uint8_t), length, file) != static_cast<size_t>(length))
		{
			::fclose(file);
			return nullptr;
		}

		::fclose(file);

		return data;
	}

}  // namespace ov