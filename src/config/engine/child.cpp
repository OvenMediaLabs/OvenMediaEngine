//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2022 AirenSoft. All rights reserved.
//
//==============================================================================
#include "./child.h"

#include "../config_private.h"
#include "./attribute.h"
#include "./item.h"
#include "./list_interface.h"

namespace cfg
{
	void Child::SetValue(const Variant &value, bool is_parent_optional)
	{
		auto &item_name		  = GetItemName();
		auto name			  = item_name.GetName(DataType::Json);

		ov::String child_path = name;

		if (value.HasValue() == false)
		{
			if (IsOptional() == false)
			{
				if (is_parent_optional == false)
				{
					throw CreateConfigError("%s is not optional", child_path.CStr());
				}

				// Parent is an optional - set a default value
				is_parent_optional = true;
			}
			else
			{
				auto error = CallOptionalCallback();

				if (error != nullptr)
				{
					throw *(error.get());
				}

				// Callback returns no error. Use default value
			}

			_is_parsed = false;
			return;
		}

		logat("Trying to cast %s for [%s] (value type: %s)",
			  ov::Demangle(value.GetTypeName()).CStr(),
			  item_name.ToString().CStr(),
			  StringFromValueType(GetType()));

		auto value_type = GetType();
		Json::Value original_value;

		try
		{
			switch (value_type)
			{
				case ValueType::Unknown:
					[[fallthrough]];
				case ValueType::String:
					[[fallthrough]];
				case ValueType::Integer:
					[[fallthrough]];
				case ValueType::Long:
					[[fallthrough]];
				case ValueType::Boolean:
					[[fallthrough]];
				case ValueType::Double:
					[[fallthrough]];
				case ValueType::Attribute:
					[[fallthrough]];
				case ValueType::Text:
					_member_pointer.SetValue(value_type, value, &original_value);
					break;

				case ValueType::Item:
					_member_pointer.TryCast<Item *>()->FromDataSource(child_path, item_name, value.TryCast<const cfg::DataSource &>());
					break;

				case ValueType::List:
					GetSharedPtrAs<ListInterface>()->AddChildValueList(value.TryCast<std::vector<cfg::DataSource>>());
					break;
			}
		}
		catch (const CastException &cast_exception)
		{
			HANDLE_CAST_EXCEPTION(GetType(), );
		}

		auto validation_error = CallValidationCallback();
		if (validation_error != nullptr)
		{
			throw *(validation_error.get());
		}

		_original_value = std::move(original_value);
		_is_parsed		= true;
	}

	void Child::SetValue(const ov::String &item_path, const DataSource &data_source, bool is_parent_optional)
	{
		auto &item_name = GetItemName();

		Json::Value original_value;
		auto value			  = data_source.GetValue(GetType(), item_name, ResolvePath(), OmitJsonName(), &original_value);
		auto name			  = item_name.GetName(DataType::Json);

		ov::String child_path = ov::String::FormatString(
			"%s%s%s",
			item_path.CStr(),
			item_path.IsEmpty() ? "" : ".",
			name.CStr());

		if (value.HasValue() == false)
		{
			if (IsOptional() == false)
			{
				if (is_parent_optional == false)
				{
					throw CreateConfigError("%s is not optional", child_path.CStr());
				}

				// Parent is an optional - set a default value
				is_parent_optional = true;
			}
			else
			{
				auto error = CallOptionalCallback();

				if (error != nullptr)
				{
					throw *(error.get());
				}

				// Callback returns no error. Use default value
			}

			_is_parsed = false;
			return;
		}

		logat("[%s] Trying to cast %s for [%s] (value type: %s)",
			  item_path.CStr(),
			  ov::Demangle(value.GetTypeName()).CStr(),
			  item_name.ToString().CStr(),
			  StringFromValueType(GetType()));

		auto value_type = GetType();

		try
		{
			switch (value_type)
			{
				case ValueType::Unknown:
					[[fallthrough]];
				case ValueType::String:
					[[fallthrough]];
				case ValueType::Integer:
					[[fallthrough]];
				case ValueType::Long:
					[[fallthrough]];
				case ValueType::Boolean:
					[[fallthrough]];
				case ValueType::Double:
					[[fallthrough]];
				case ValueType::Attribute:
					[[fallthrough]];
				case ValueType::Text:
					_member_pointer.SetValue(value_type, value);
					break;

				case ValueType::Item:
					_member_pointer.TryCast<Item *>()->FromDataSource(child_path, item_name, value.TryCast<const cfg::DataSource &>(), is_parent_optional);
					break;

				case ValueType::List:
					GetSharedPtrAs<ListInterface>()->AddChildValueList(value.TryCast<std::vector<cfg::DataSource>>());
					break;
			}
		}
		catch (const CastException &cast_exception)
		{
			HANDLE_CAST_EXCEPTION(GetType(), "[%s] ", item_path.CStr());
		}

		auto validation_error = CallValidationCallback();
		if (validation_error != nullptr)
		{
			throw *(validation_error.get());
		}

		_original_value = std::move(original_value);
		_is_parsed		= true;
	}

}  // namespace cfg
