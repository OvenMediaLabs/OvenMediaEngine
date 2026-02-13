//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2021 AirenSoft. All rights reserved.
//
//==============================================================================
#include "value_type.h"

#include "attribute.h"
#include "item.h"
#include "list_interface.h"

namespace cfg
{
	const char *StringFromValueType(ValueType type)
	{
		switch (type)
		{
			case ValueType::Unknown:
				break;

			case ValueType::String:
				return "String";

			case ValueType::Integer:
				return "Integer";

			case ValueType::Long:
				return "Long";

			case ValueType::Boolean:
				return "Boolean";

			case ValueType::Double:
				return "Double";

			case ValueType::Attribute:
				return "Attribute";

			case ValueType::Text:
				return "Text";

			case ValueType::Item:
				return "Item";

			case ValueType::List:
				return "List";
		}

		return "Unknown";
	}

	constexpr const char *INDENTATION = "    ";
	ov::String MakeIndentString(int indent_count)
	{
		return ov::String::Repeat(INDENTATION, indent_count);
	}

	ov::String ToDebugString(int indent_count, const ov::String *value)
	{
		return ov::String::FormatString("\"%s\"", value->CStr());
	}

	ov::String ToDebugString(int indent_count, const int *value)
	{
		return ov::Converter::ToString(*value);
	}

	ov::String ToDebugString(int indent_count, const int64_t *value)
	{
		auto result = ov::Converter::ToString(*value);
		result += "L";
		return result;
	}

	ov::String ToDebugString(int indent_count, const bool *value)
	{
		return ov::Converter::ToString(*value);
	}

	ov::String ToDebugString(int indent_count, const double *value)
	{
		return ov::Converter::ToString(*value);
	}

	ov::String ToDebugString(int indent_count, const Attribute *value)
	{
		return ov::String::FormatString("\"%s\"", value->GetValue().CStr());
	}

	ov::String ToDebugString(int indent_count, const Text *value)
	{
		return ov::String::FormatString("\"%s\"", value->ToString().CStr());
	}

	ov::String ToDebugString(int indent_count, const Item *value)
	{
		return value->ToString(indent_count);
	}

	ov::String ToString(const Text *value)
	{
		return value->ToString();
	}

	ov::String ToString(int indent_count, const Item *value)
	{
		return value->ToString(indent_count);
	}

}  // namespace cfg
