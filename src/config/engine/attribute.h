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

namespace cfg
{
	class Variant;

	class Attribute
	{
	protected:
		friend class Variant;

	public:
		const ov::String &GetValue() const
		{
			return _value;
		}

		operator const char *() const
		{
			return _value;
		}

	protected:
		void FromString(const ov::String &str)
		{
			_value = str;
		}

		ov::String _value;
	};
}  // namespace cfg
