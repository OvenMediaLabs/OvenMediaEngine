//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2025 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include "../../string.h"
#include "./common.h"

namespace ov::logger
{
	class StringFormatter : protected fmt::formatter<std::string_view>
	{
	public:
		constexpr ParseResult Parse(ParseContext &ctx)
		{
			return fmt::formatter<std::string_view>::parse(ctx);
		}

		FormatResult Format(String const &instance, FormatContext &ctx) const;
	};
}  // namespace ov::logger

DECLARE_CUSTOM_FORMATTER(ov::logger::StringFormatter, ov::String);
