//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2022 AirenSoft. All rights reserved.
//
//==============================================================================
#include "./list_interface.h"

#include "../config_private.h"
#include "./item.h"

namespace cfg
{
	ListInterface::ListInterface(const ItemName &item_name, const ov::String &type_name,
								 cfg::Optional is_optional, cfg::ResolvePath resolve_path, cfg::OmitJsonName omit_json_name,
								 OptionalCallback optional_callback, ValidationCallback validation_callback,
								 void *member_raw_pointer, std::any member_pointer, ValueType list_item_type)
		: Child(item_name, ValueType::List, type_name,
				is_optional, resolve_path, omit_json_name,
				optional_callback, validation_callback,
				member_raw_pointer, std::move(member_pointer)),

		  _list_item_type(list_item_type)
	{
	}

	void ListInterface::CopyFrom(const std::shared_ptr<const ListInterface> &another_list)
	{
		_item_name			 = another_list->_item_name;

		_is_parsed			 = another_list->_is_parsed;

		_original_value		 = another_list->_original_value;
		_original_value_list = another_list->_original_value_list;
	}

	void ListInterface::SetJsonChildValue(ValueType value_type, Json::Value &object, const ov::String &child_name, const Json::Value &original_value)
	{
		Item::SetOriginalJsonValue(value_type, object, child_name, original_value);
	}

	void ListInterface::ValidateOmitJsonNameRule(const ov::String &path, const ItemName &item_name) const
	{
		auto value_type = GetListItemType();

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
				OV_ASSERT(value_type == ValueType::List, "ListInterface should contains Item or List type, but: %s", StringFromValueType(value_type));
				throw CreateConfigError("Internal config module error");

			case ValueType::Item:
				[[fallthrough]];
			// Since ListInterface does not know exactly the types of sub-items at this point, sub-class is responsible for validation.
			case ValueType::List:
				ValidateOmitJsonNameRuleInternal(path, item_name);
		}
	}

	void ListInterface::AddChildValueList(const std::vector<cfg::DataSource> &data_source_list)
	{
		std::vector<DataSource> new_data_sources;
		const auto &item_name = GetItemName();
		auto list_item_type	  = GetListItemType();
		auto resolve_path	  = ResolvePath();
		auto omit_json		  = OmitJsonName();

		Clear();

		for (auto &data_source : data_source_list)
		{
			if (data_source.IsSourceOf(item_name))
			{
				// Check the child has an include attribute
				std::vector<DataSource> data_sources_for_array;
				std::vector<ov::String> include_files;

				if (data_source.GetIncludeFileList(nullptr, &include_files))
				{
					for (auto &include_file : include_files)
					{
						AppendChildValue(
							data_source.NewDataSource(include_file, item_name),
							list_item_type,
							resolve_path,
							omit_json);
					}
				}
				else
				{
					// "include" attribute is not present
					AppendChildValue(
						data_source,
						list_item_type,
						resolve_path,
						omit_json);
				}
			}
			else
			{
				// The parent has multiple types of items
			}
		}
	}

}  // namespace cfg
