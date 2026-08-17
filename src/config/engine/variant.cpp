//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2022 AirenSoft. All rights reserved.
//
//==============================================================================
#include "./variant.h"

#include "./attribute.h"
#include "./text.h"

#define SET_IF_NOT_NULL(variable, value) \
	if (variable != nullptr)             \
	{                                    \
		*variable = (value);             \
	}

namespace cfg
{
	void Variant::SetValue(ValueType value_type, const Variant &value, Json::Value *original_value)
	{
		switch (value_type)
		{
			case ValueType::Unknown:
				[[fallthrough]];
			case ValueType::Item:
				[[fallthrough]];
			case ValueType::List:
				throw CreateConfigError("%s is not supported ", StringFromValueType(value_type));

			case ValueType::String: {
				auto converted			   = value.TryCast<ov::String, ov::String, const char *>();
				*(TryCast<ov::String *>()) = converted;
				SET_IF_NOT_NULL(original_value, converted.CStr());
				break;
			}

			case ValueType::Integer: {
				auto converted		= value.TryCast<int, int, long, int64_t>();
				*(TryCast<int *>()) = converted;
				SET_IF_NOT_NULL(original_value, converted);
				break;
			}

			case ValueType::Long: {
				auto converted			= value.TryCast<int, int64_t, long, int>();
				*(TryCast<int64_t *>()) = converted;
				SET_IF_NOT_NULL(original_value, converted);
				break;
			}

			case ValueType::Boolean: {
				auto converted		 = value.TryCast<bool, bool, int>();
				*(TryCast<bool *>()) = converted;
				SET_IF_NOT_NULL(original_value, converted);
				break;
			}

			case ValueType::Double: {
				auto converted		   = value.TryCast<double, double, float>();
				*(TryCast<double *>()) = converted;
				SET_IF_NOT_NULL(original_value, converted);
				break;
			}

			case ValueType::Attribute: {
				auto converted = value.TryCast<ov::String, ov::String, const char *>();
				TryCast<Attribute *>()->FromString(converted);
				SET_IF_NOT_NULL(original_value, converted.CStr());
				break;
			}

			case ValueType::Text: {
				auto converted = value.TryCast<ov::String, ov::String, const char *>();
				TryCast<Text *>()->FromString(converted);
				SET_IF_NOT_NULL(original_value, converted.CStr());
				break;
			}
		}
	}

}  // namespace cfg
