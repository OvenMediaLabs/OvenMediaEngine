//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include "./string.h"

namespace ov
{
	// Dump utilities

	String Demangle(const char *func);

	// Convert data to hex string (like "0001020304A0")
	String ToHexString(const void *data, size_t length);
	// Convert data to hex string with delimeter (like edef8ba9-79d6-4ace-a3c8-27dcd51d21ed)
	String ToUUIDString(const void *data, size_t length);
	// Convert data to hex string with delimeter (like "00:01:02:03:04:A0");
	String ToHexStringWithDelimiter(const void *data, size_t length, char delimiter);
	String ToHexStringWithDelimiter(const Data *data, char delimiter);

	// Hex dump (for debugging)
	String Dump(const void *data, size_t length, const char *title, off_t offset = 0, size_t max_bytes = 1024, const char *line_prefix = nullptr) noexcept;
	String Dump(const void *data, size_t length, size_t max_bytes = 1024) noexcept;

	// Write data to file
	std::shared_ptr<FILE> DumpToFile(const char *file_name, const void *data, size_t length, off_t offset = 0, bool append = false) noexcept;
	std::shared_ptr<FILE> DumpToFile(const char *file_name, const std::shared_ptr<const Data> &data, off_t offset = 0, bool append = false) noexcept;

	std::shared_ptr<Data> LoadFromFile(const char *file_name) noexcept;
}
