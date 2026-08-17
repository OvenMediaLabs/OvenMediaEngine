//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================
#include "./json.h"

#include "./ovlibrary_private.h"

namespace ov
{
	constexpr const char *StringFromJsonValueType(::Json::ValueType value_type)
	{
		switch (value_type)
		{
			case ::Json::ValueType::nullValue:
				return "null";
			case ::Json::ValueType::intValue:
				return "int";
			case ::Json::ValueType::uintValue:
				return "uint";
			case ::Json::ValueType::realValue:
				return "real";
			case ::Json::ValueType::stringValue:
				return "string";
			case ::Json::ValueType::booleanValue:
				return "boolean";
			case ::Json::ValueType::arrayValue:
				return "array";
			case ::Json::ValueType::objectValue:
				return "object";
		}

		return "unknown";
	}

	const char *StringFromJsonValueType(const ::Json::Value &value)
	{
		return StringFromJsonValueType(value.type());
	}

	ov::String Json::Stringify(const JsonObject &object)
	{
		return Stringify(object._value);
	}

	ov::String Json::Stringify(const ::Json::Value &value)
	{
		return Stringify(value, false);
	}

	ov::String Json::Stringify(const ::Json::Value &value, bool prettify)
	{
		::Json::StreamWriterBuilder builder;

		if (prettify == false)
		{
			builder["indentation"] = "";
		}

		std::unique_ptr<::Json::StreamWriter> const writer(builder.newStreamWriter());

		std::ostringstream stream;

		if (writer->write(value, &stream) == 0)
		{
			return String(stream.str().c_str());
		}

		return "";
	}

	JsonObject Json::Parse(const ov::String &str)
	{
		JsonObject object;

		auto error = object.Parse(str);

		if (error == nullptr)
		{
			return object;
		}

		logtw("Could not parse: %s", error->What());

		return JsonObject::NullObject();
	}

	JsonObject Json::Parse(const std::shared_ptr<const Data> &data)
	{
		JsonObject object;

		auto error = object.Parse(data);

		if (error == nullptr)
		{
			return object;
		}

		logtw("Could not parse: %s", error->What());

		return JsonObject::NullObject();
	}
}  // namespace ov
