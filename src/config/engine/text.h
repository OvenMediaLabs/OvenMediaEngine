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

#include "config_error.h"

namespace cfg
{
	class Variant;

	class Text
	{
	protected:
		friend class Variant;

	public:
		virtual ov::String ToString() const = 0;

		virtual ov::String ToString(int indent) const
		{
			return ov::String::FormatString(
				"%s%s",
				MakeIndentString(indent).CStr(),
				ToString().CStr());
		}

	protected:
		static ov::String MakeIndentString(int indent_count)
		{
			static constexpr const char *INDENTATION = "    ";

			if (indent_count > 0)
			{
				ov::String indent_string;

				for (int index = 0; index < indent_count; index++)
				{
					indent_string += INDENTATION;
				}

				return indent_string;
			}

			return "";
		}

		MAY_THROWS(cfg::ConfigError)
		virtual void FromString(const ov::String &str) = 0;

		MAY_THROWS(cfg::ConfigError)
		bool IsParsed(const void *target) const
		{
			throw CreateConfigError("Use Item::IsParsed() instead");
		}
	};
}  // namespace cfg
