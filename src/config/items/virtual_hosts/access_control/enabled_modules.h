//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2020 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

namespace cfg
{
	namespace vhost
	{
		namespace sig
		{
			struct EnabledModules : public Text
			{
				CFG_DECLARE_CONST_REF_GETTER_OF(GetValue, _value)
				CFG_DECLARE_CONST_REF_GETTER_OF(GetValueList, _value_list)

				bool IsExist(ov::String value) const
				{
					value = value.UpperCaseString();

					for (const auto &item : _value_list)
					{
						auto config_value = item.Trim();
						if (config_value == value)
						{
							return true;
						}
					}

					return false;
				}

				ov::String ToString() const override
				{
					return _value;
				}

			protected:
				void FromString(const ov::String &str) override
				{
					_value = str;
					_value_list = _value.UpperCaseString().Trim().Split(",");
				}

				ov::String _value;
				std::vector<ov::String> _value_list;
			};
		}  // namespace sig
	}	   // namespace vhost
}  // namespace cfg