//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================
#include "./json_object.h"
#include "./json.h"
#include "./ovlibrary_private.h"
#include "./dump_utilities.h"

namespace ov
{
	JsonObject::JsonObject()
	{
	}

	JsonObject::~JsonObject()
	{
	}

	std::shared_ptr<Error> JsonObject::Parse(const void *data, ssize_t length)
	{
		::Json::Value root;

		::Json::CharReaderBuilder builder;
		std::unique_ptr<::Json::CharReader> const reader(builder.newCharReader());

		std::string error;

		bool result = reader->parse(static_cast<const char *>(data), static_cast<const char *>(data) + length, &root, &error);

		if(result == false)
		{
			logtw("An error occurred while parsing json: %s\n %s", error.c_str(), ov::Dump(data, length).CStr());
			return Error::CreateError("JSON", "%s", error.c_str());
		}

		_value = root;

		return nullptr;
	}

	String JsonObject::ToString() const
	{
		if(_value.isString())
		{
			return _value.asCString();
		}

		return ov::Json::Stringify(_value);
	}
}
