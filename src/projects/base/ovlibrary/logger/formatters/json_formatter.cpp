//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2025 AirenSoft. All rights reserved.
//
//==============================================================================
#include "./json_formatter.h"

namespace ov::logger
{
	FormatResult JsonFormatter::Format(::Json::Value const &instance, FormatContext &ctx) const
	{
		auto json_str = ov::Json::Stringify(instance);

		return fmt::formatter<std::string_view>::format(
			std::string_view(json_str.CStr(), json_str.GetLength()), ctx);
	}
}  // namespace ov::logger
