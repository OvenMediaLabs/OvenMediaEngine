//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2021 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <base/ovlibrary/ovlibrary.h>

#include <memory>

#define CreateConfigErrorPtr(format, ...) \
	std::make_shared<cfg::ConfigError>(__FILE__, __LINE__, format, ##__VA_ARGS__)

#define CreateConfigError(format, ...) \
	cfg::ConfigError(__FILE__, __LINE__, format, ##__VA_ARGS__)

namespace cfg
{
	class ConfigError : public ov::Error
	{
	public:
		ConfigError(const char *file_name, int line_number, const char *format, ...);

		ov::String GetDetailedMessage() const;

	protected:
		ov::String _file_name;
		int _line_number;
	};
}  // namespace cfg
