//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2019 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include "./option.h"

namespace cfg
{
	namespace cmn
	{
		struct CrossDomains : public Item
		{
		protected:
			std::vector<ov::String> _url_list;
			std::vector<cmn::Option> _custom_headers;
			std::map<ov::String, ov::String> _custom_header_map;

		public:
			CFG_DECLARE_CONST_REF_GETTER_OF(GetUrls, _url_list)
			CFG_DECLARE_CONST_REF_GETTER_OF(GetCustomHeaders, _custom_headers)

			// Set * to allow all domains
			void AllowAll()
			{
				_url_list.clear();
				_url_list.emplace_back("*");
			}

			// Return original case of the key and value
			std::optional<std::tuple<ov::String, ov::String>> GetCustomHeader(ov::String key) const
			{
				auto it = _custom_header_map.find(key.LowerCaseString());
				if (it == _custom_header_map.end())
				{
					return std::nullopt;
				}

				auto key_value		= it->second;
				auto key_value_list = key_value.Split(":");
				if (key_value_list.size() != 2)
				{
					return std::nullopt;
				}

				auto origin_key = key_value_list[0];
				auto value		= key_value_list[1];

				return std::make_tuple(origin_key, value);
			}

		protected:
			void MakeList() override
			{
				Register<Optional>({"Url", "urls"}, &_url_list);
				Register<Optional>({"Header", "headers"}, &_custom_headers, nullptr,
								   [=]() -> std::shared_ptr<ConfigError> {
									   for (auto &item : _custom_headers)
									   {
										   auto key		  = item.GetKey().LowerCaseString();
										   auto key_value = ov::String::FormatString("%s:%s", item.GetKey().CStr(), item.GetValue().CStr());
										   // To keep the original case of the key
										   _custom_header_map.emplace(key, key_value);
									   }
									   return nullptr;
								   });
			}
		};
	}  // namespace cmn
}  // namespace cfg
