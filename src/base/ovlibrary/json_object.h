//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <jsoncpp-1.9.3/json/json.h>

#include "./data.h"
#include "./error.h"
#include "./string.h"

namespace ov
{
	class JsonObject
	{
	public:
		friend class Json;

		JsonObject();
		~JsonObject();

		JsonObject(JsonObject &&object)
		{
			std::swap(_value, object._value);
		}

		JsonObject(const ::Json::Value &value)
		{
			_value = value;
		}

		static JsonObject NullObject()
		{
			static ::Json::Value null_value;

			return JsonObject(null_value);
		}

		bool IsMember() const noexcept
		{
			return _value.asInt();
		}

		bool IsNull() const noexcept
		{
			return _value.isNull();
		}

		bool IsArray() const noexcept
		{
			return _value.isArray();
		}

		bool IsString() const noexcept
		{
			return _value.isString();
		}

		bool IsObject() const noexcept
		{
			return _value.isObject();
		}

		// jsoncpp를 wrapping 하기 전까지 사용할 임시 인터페이스
		::Json::Value &GetJsonValue()
		{
			return _value;
		}

		int GetIntValue(const ov::String &key) const
		{
			auto &value = _value[key];

			if (value.isIntegral())
			{
				return value.asInt();
			}

			return 0;
		}

		int64_t GetInt64Value(const ov::String &key) const
		{
			auto &value = _value[key];

			if (value.isIntegral())
			{
				return value.asInt64();
			}

			return 0;
		}

		String GetStringValue(const ov::String &key) const
		{
			auto &value = _value[key];

			if (value.isString())
			{
				return ov::String(value.asString().c_str());
			}

			return nullptr;
		}

		bool GetBoolValue(const ov::String &key) const
		{
			auto &value = _value[key];

			if (value.isBool())
			{
				return value.asBool();
			}

			return false;
		}

		const ::Json::Value &GetJsonValue(const ov::String &key) const
		{
			return _value[key];
		}

		bool IsMember(const ov::String &key) const
		{
			return _value.isMember(key.CStr());
		}

		String ToString() const;

		std::shared_ptr<Error> Parse(const String &str)
		{
			return Parse(str.CStr(), str.GetLength());
		}

		std::shared_ptr<Error> Parse(const std::shared_ptr<const Data> &data)
		{
			if ((data == nullptr) || (data->GetData() == nullptr))
			{
				_value = ::Json::nullValue;
				return nullptr;
			}

			return Parse(data->GetData(), data->GetLength());
		}

	protected:
		std::shared_ptr<Error> Parse(const void *data, ssize_t length);

		::Json::Value _value;
	};
}  // namespace ov
