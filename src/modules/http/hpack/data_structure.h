//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2022 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <base/ovlibrary/ovlibrary.h>

namespace http
{
	namespace hpack
	{
		class HeaderField
		{
		public:
			HeaderField() = default;

			HeaderField(ov::String name, ov::String value)
				: _name(std::move(name)), _value(std::move(value))
			{
			}

			void SetNameValue(const ov::String &name, const ov::String &value)
			{
				_name = name;
				_value = value;
			}

			ov::String GetName() const
			{
				return _name;
			}

			ov::String GetValue() const
			{
				return _value;
			}

			ov::String GetKey() const
			{
				return (_name + _value);
			}

			size_t GetSize() const
			{
				// https://www.rfc-editor.org/rfc/rfc7541.html#section-4.1
				// The size of an entry is the sum of its name's length in octets (as
   				// defined in Section 5.2), its value's length in octets, and 32.	

				return _name.GetLength() + _value.GetLength() + 32;
			}

			ov::String ToString() const
			{
				return ov::String::FormatString("%s: %s", _name.CStr(), _value.CStr());
			}

		private:
			ov::String _name;
			ov::String _value;
		};

		
	} // namespace hpack
} // namespace http