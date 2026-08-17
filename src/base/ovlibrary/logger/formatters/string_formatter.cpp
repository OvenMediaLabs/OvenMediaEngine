//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2025 AirenSoft. All rights reserved.
//
//==============================================================================
#include "./string_formatter.h"

namespace ov::logger
{
	FormatResult StringFormatter::Format(String const &instance, FormatContext &ctx) const
	{
		return fmt::formatter<std::string_view>::format(
			std::string_view(instance.CStr(), instance.GetLength()), ctx);
	}
}  // namespace ov::logger
