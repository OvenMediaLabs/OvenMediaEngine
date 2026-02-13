//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include "./json_builder.h"

namespace ov
{
	constexpr const char *StringFromJsonValueType(::Json::ValueType value_type);
	const char *StringFromJsonValueType(const ::Json::Value &value);

	class Json
	{
	public:
		Json() = delete;
		~Json() = delete;

		static ov::String Stringify(const JsonObject &object);
		static ov::String Stringify(const ::Json::Value &value);
		static ov::String Stringify(const ::Json::Value &value, bool prettify);

		static JsonObject Parse(const ov::String &str);
		static JsonObject Parse(const std::shared_ptr<const Data> &data);
	};
}  // namespace ov
