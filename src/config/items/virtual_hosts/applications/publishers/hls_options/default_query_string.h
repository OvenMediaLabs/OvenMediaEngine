//=============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2024 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include "../../../../common/option.h"

namespace cfg
{
	namespace vhost
	{
		namespace app
		{
			namespace pub
			{
				struct DefaultQueryString : public Item
				{
				protected:
					std::vector<cmn::Option> _items;
					std::map<ov::String, ov::String> _item_map;

				public:
					CFG_DECLARE_CONST_REF_GETTER_OF(GetItems, _items)

					ov::String GetValue(ov::String key) const
					{
						auto it = _item_map.find(key);
						if (it != _item_map.end())
						{
							return it->second;
						}
						return "";
					}

					bool GetBoolValue(ov::String key, bool default_value) const
					{
						auto it = _item_map.find(key);
						if (it != _item_map.end())
						{
							ov::String value = it->second;

							if (value.UpperCaseString() == "TRUE" || value.UpperCaseString() == "YES" || value.UpperCaseString() == "ON" || value.UpperCaseString() == "1")
							{
								return true;
							}
							else
							{
								return false;
							}
						}

						return default_value;
					}

				protected:
					void MakeList() override
					{
						Register<Optional>({"Query", "queries"}, &_items, nullptr,
										   [=]() -> std::shared_ptr<ConfigError> {
											   for (auto &item : _items)
											   {
												   auto key	  = item.GetKey();
												   auto value = item.GetValue();
												   _item_map.emplace(key, value);
											   }
											   return nullptr;
										   });
					}
				};
			}  // namespace pub
		}  // namespace app
	}  // namespace vhost
}  // namespace cfg
