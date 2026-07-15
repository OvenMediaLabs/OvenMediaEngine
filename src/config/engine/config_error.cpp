//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2021 AirenSoft. All rights reserved.
//
//==============================================================================
#include "config_error.h"

namespace cfg
{
	ConfigError::ConfigError(const char *file_name, int line_number, const char *format, ...)
		: ov::Error("Config"),

		  _file_name(file_name),
		  _line_number(line_number)
	{
		ov::String message;
		va_list list;
		va_start(list, format);
		message.VFormat(format, list);
		va_end(list);

		SetMessage(std::move(message));
	}

	ov::String ConfigError::GetDetailedMessage() const
	{
		ov::String message = what();

		message.AppendFormat(" (%s:%d)", _file_name.CStr(), _line_number);

		return message;
	}
}  // namespace cfg
