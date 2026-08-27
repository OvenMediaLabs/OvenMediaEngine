//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2020 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <base/ovlibrary/ovlibrary.h>

#include <optional>

#include "./converter_private.h"

#define CONVERTER_RETURN_IF(condition, default_value)          \
	if (condition)                                             \
	{                                                          \
		if (optional == Optional::False)                       \
		{                                                      \
			OV_ASSERT2(#condition);                            \
		}                                                      \
                                                               \
		return;                                                \
	}                                                          \
	[[maybe_unused]] Json::Value &object = (key == nullptr) ? parent_object : parent_object[key]; \
	if (object.isNull())                                       \
	{                                                          \
		object = default_value;                                \
	}

namespace serdes
{
	enum class Optional
	{
		True,
		False
	};

	inline void SetString(Json::Value &parent_object, const char *key, const ov::String &value, Optional optional)
	{
		CONVERTER_RETURN_IF(value.IsEmpty(), Json::stringValue);

		object = value.CStr();
	}

	inline void SetInt(Json::Value &parent_object, const char *key, int32_t value)
	{
		parent_object[key] = value;
	}

	inline void SetInt64(Json::Value &parent_object, const char *key, int64_t value)
	{
		parent_object[key] = value;
	}

	inline void SetFloat(Json::Value &parent_object, const char *key, float value)
	{
		parent_object[key] = value;
	}

	inline void SetTimeInterval(Json::Value &parent_object, const char *key, int64_t value)
	{
		parent_object[key] = value;
	}

	void SetTimestamp(Json::Value &parent_object, const char *key, const std::chrono::system_clock::time_point &time_point);

	inline void SetBool(Json::Value &parent_object, const char *key, bool value)
	{
		parent_object[key] = value;
	}

	// std::optional overloads: the key is emitted only when the value is present
	inline void SetInt(Json::Value &parent_object, const char *key, const std::optional<int32_t> &value)
	{
		if (value.has_value())
		{
			SetInt(parent_object, key, value.value());
		}
	}

	inline void SetInt64(Json::Value &parent_object, const char *key, const std::optional<int64_t> &value)
	{
		if (value.has_value())
		{
			SetInt64(parent_object, key, value.value());
		}
	}

	inline void SetFloat(Json::Value &parent_object, const char *key, const std::optional<float> &value)
	{
		if (value.has_value())
		{
			SetFloat(parent_object, key, value.value());
		}
	}

	inline void SetBool(Json::Value &parent_object, const char *key, const std::optional<bool> &value)
	{
		if (value.has_value())
		{
			SetBool(parent_object, key, value.value());
		}
	}
}  // namespace serdes