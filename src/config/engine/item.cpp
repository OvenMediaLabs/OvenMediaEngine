//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2021 AirenSoft. All rights reserved.
//
//==============================================================================
#include "item.h"

#include "../config_private.h"

namespace cfg
{
	MAY_THROWS(cfg::ConfigError)
	const Json::Value &GetJsonObject(const Json::Value &value, const char *key)
	{
		try
		{
			return value[key];
		}
		catch (const std::exception &e)
		{
			logtw("Could not obtain JSON value for key %s\nException: %s\n%s", key, e.what(), ov::Json::Stringify(value).CStr());
			throw CreateConfigError("JSON value is not an object (Expected: object, but: %s)", ov::StringFromJsonValueType(value));
		}
	}

	MAY_THROWS(cfg::ConfigError)
	Json::Value &GetJsonObject(Json::Value &value, const char *key)
	{
		try
		{
			return value[key];
		}
		catch (const std::exception &e)
		{
			logtw("Could not obtain JSON value for key %s\nException: %s\n%s", key, e.what(), ov::Json::Stringify(value).CStr());
			throw CreateConfigError("JSON value is not an object (Expected: object, but: %s)", ov::StringFromJsonValueType(value));
		}
	}

	ov::String Item::ChildToString(int indent_count, const std::shared_ptr<const Child> &child, size_t index, size_t child_count)
	{
		ov::String indent = MakeIndentString(indent_count + 1);

		ov::String description;
		ov::String extra;

		ov::String child_name = child->GetItemName().ToString();
		auto value_type		  = child->GetType();

		ov::String comma	  = (index < (child_count - 1)) ? "," : "";

#if CFG_VERBOSE_STRING
		extra.Format(
			CFG_EXTRA_PREFIX
#	if CFG_PRINT_MEMORY_ADDRESS
			"%p (%p): "
#	endif	// CFG_PRINT_MEMORY_ADDRESS
			"%s%s%s (%s), "
			"%s"
			"%s"
			"%s",
#	if CFG_PRINT_MEMORY_ADDRESS
			child->GetMemberPointer(), child.get(),
#	endif	// CFG_PRINT_MEMORY_ADDRESS
			(value_type == ValueType::List) ? "std::vector<" : "", child->GetTypeName().CStr(), (value_type == ValueType::List) ? ">" : "", StringFromValueType(child->GetType()),
			child->IsOptional() ? "Optional, " : "",
			child->ResolvePath() ? "Path, " : "",
			child->IsParsed() ? "Parsed" : "NotParsed");

		if (child->IsParsed() &&
			((child->GetType() != ValueType::Item) && (child->GetType() != ValueType::List)))
		{
			extra.AppendFormat(" - \"%s\"", ov::Converter::ToString(child->GetOriginalValue()).CStr());
		}

		extra.Append(CFG_EXTRA_SUFFIX);
#endif	// CFG_VERBOSE_STRING

#define APPEND_DESCRIPTION(key_format, expression) \
	description.AppendFormat(                      \
		"%s" key_format                            \
		": %s"                                     \
		"%s%s",                                    \
		indent.CStr(), child_name.CStr(),          \
		expression,                                \
		comma.CStr(), extra.CStr())

		switch (value_type)
		{
			case ValueType::Unknown:
				APPEND_DESCRIPTION("%s", "<Unknown type>");
				break;
			case ValueType::String:
				APPEND_DESCRIPTION("%s", ToDebugString(indent_count + 1, child->GetMemberPointerAs<ov::String>()).CStr());
				break;
			case ValueType::Integer:
				APPEND_DESCRIPTION("%s", ToDebugString(indent_count + 1, child->GetMemberPointerAs<int>()).CStr());
				break;
			case ValueType::Long:
				APPEND_DESCRIPTION("%s", ToDebugString(indent_count + 1, child->GetMemberPointerAs<int64_t>()).CStr());
				break;
			case ValueType::Boolean:
				APPEND_DESCRIPTION("%s", ToDebugString(indent_count + 1, child->GetMemberPointerAs<bool>()).CStr());
				break;
			case ValueType::Double:
				APPEND_DESCRIPTION("%s", ToDebugString(indent_count + 1, child->GetMemberPointerAs<double>()).CStr());
				break;
			case ValueType::Attribute:
				APPEND_DESCRIPTION("$.%s", ToDebugString(indent_count + 1, child->GetMemberPointerAs<Attribute>()).CStr());
				break;
			case ValueType::Text:
				APPEND_DESCRIPTION("[%s]", ToDebugString(indent_count + 1, child->GetMemberPointerAs<Text>()).CStr());
				break;
			case ValueType::Item:
				description.AppendFormat("%s%s", ToDebugString(indent_count + 1, child->GetMemberPointerAs<Item>()).CStr(), comma.CStr());
				break;
			case ValueType::List: {
				auto list_item = std::dynamic_pointer_cast<const ListInterface>(child);

				if (list_item->GetCount() > 0)
				{
					description.AppendFormat(
						"%s%s: [%s\n"
						"%s"
						"%s]%s",
						indent.CStr(), child_name.CStr(), extra.CStr(),
						list_item->ToString(indent_count + 2, child).CStr(),
						indent.CStr(), comma.CStr());
				}
				else
				{
					APPEND_DESCRIPTION("%s", "[]");
				}

				break;
			}
		}

		return description;
	}

	Item::Item(const Item &item)
	{
		_last_target	   = nullptr;

		_is_parsed		   = item._is_parsed;
		_is_read_only	   = item._is_read_only;

		_item_name		   = item._item_name;

		_children		   = item._children;
		_children_for_xml  = item._children_for_xml;
		_children_for_json = item._children_for_json;
	}

	Item::Item(Item &&item)
	{
		_last_target	  = nullptr;
		item._last_target = nullptr;

		std::swap(_is_parsed, item._is_parsed);
		std::swap(_is_read_only, item._is_read_only);

		std::swap(_item_name, item._item_name);

		std::swap(_children, item._children);
		std::swap(_children_for_xml, item._children_for_xml);
		std::swap(_children_for_json, item._children_for_json);
	}

	Item &Item::operator=(const Item &item)
	{
		_last_target	   = nullptr;

		_is_parsed		   = item._is_parsed;
		_is_read_only	   = item._is_read_only;

		_item_name		   = item._item_name;

		_children		   = item._children;
		_children_for_xml  = item._children_for_xml;
		_children_for_json = item._children_for_json;

		RebuildListIfNeeded();

		return *this;
	}

	void Item::RebuildListIfNeeded() const
	{
		// Change constness
		(const_cast<Item *>(this))->RebuildListIfNeeded();
	}

	void Item::RebuildListIfNeeded()
	{
		if (_last_target != this)
		{
			logat("[%s] Rebuilding a list of children (last: %p, current: %p)", _item_name.ToString().CStr(), _last_target, this);

			_last_target = this;

			MakeList();
		}
	}

	void Item::AddChild(const ItemName &name, ValueType type, const ov::String &type_name,
						Optional is_optional, cfg::ResolvePath resolve_path, cfg::OmitJsonName omit_json_name,
						OptionalCallback optional_callback, ValidationCallback validation_callback,
						void *member_raw_pointer, std::any member_pointer)
	{
		AddChild(std::make_shared<Child>(
			name, type, type_name,
			is_optional, resolve_path, omit_json_name,
			optional_callback, validation_callback,
			member_raw_pointer, std::move(member_pointer)));
	}

	void Item::AddChild(const std::shared_ptr<Child> &child)
	{
		std::shared_ptr<const Child> prev_child;
		auto name = child->GetItemName();

		for (auto child_iterator = _children.begin(); child_iterator != _children.end(); ++child_iterator)
		{
			auto &child = *child_iterator;
			if (child->GetItemName() == name)
			{
				prev_child = *child_iterator;
				_children.erase(child_iterator);
				break;
			}
		}

		_children_for_xml.insert_or_assign(name.xml_name, child);
		_children_for_json.insert_or_assign(name.json_name, child);
		_children.push_back(child);

		if (prev_child != nullptr)
		{
			// These are immutable attributes
			OV_ASSERT2(prev_child->GetItemName() == name);
			OV_ASSERT2(prev_child->GetType() == child->GetType());
			OV_ASSERT2(prev_child->GetTypeName() == child->GetTypeName());
			OV_ASSERT2(prev_child->IsOptional() == child->IsOptional());
			OV_ASSERT2(prev_child->ResolvePath() == child->ResolvePath());

			child->CopyFrom(prev_child);
		}
	}

	void Item::FromDataSource(ov::String item_path, const ItemName &name, const DataSource &data_source, bool allow_optional)
	{
		_item_name = name;

		RebuildListIfNeeded();

		data_source.CheckUnknownItems(item_path, _children_for_xml, _children_for_json);

		FromDataSourceInternal(item_path, data_source, allow_optional);
	}

	void Item::FromDataSource(const DataSource &data_source, bool allow_optional)
	{
		OV_ASSERT(_item_name.xml_name.IsEmpty() == false, "Item name is required");

		FromDataSource(_item_name.GetName(data_source.GetType()), _item_name, data_source, allow_optional);
	}

	void Item::FromJson(const Json::Value &value, bool allow_optional)
	{
		OV_ASSERT(_item_name.xml_name.IsEmpty() == false, "Item name is required");

		cfg::DataSource data_source("", "", _item_name.GetName(DataType::Json), value);
		FromDataSource(data_source, allow_optional);
	}

	bool Item::IsParsed(const void *target) const
	{
		RebuildListIfNeeded();

		for (const auto &child : _children)
		{
			if (child->GetMemberRawPointer() == target)
			{
				return child->IsParsed();
			}
		}

		throw CreateConfigError("Could not find target: %p", target);
	}

	void Item::ValidateOmitJsonNameRuleForItem(const ov::String &item_path, const std::shared_ptr<Child> &child) const
	{
		try
		{
			auto child_item = child->GetMemberPointerAs<Item>();
			child_item->ValidateOmitJsonNameRule(item_path, child->GetItemName().GetName(DataType::Json));
		}
		catch (const CastException &cast_exception)
		{
			HANDLE_CAST_EXCEPTION(ValueType::Item, "[%s.%s] ", item_path.CStr(), child->GetItemName().ToString().CStr());
		}
	}

	void Item::ValidateOmitJsonNameRuleForList(const ov::String &item_path, const std::shared_ptr<Child> &child) const
	{
		auto child_name = child->GetItemName().ToString();

		std::shared_ptr<ListInterface> list_interface;

		try
		{
			list_interface = std::dynamic_pointer_cast<ListInterface>(child);
		}
		catch (const CastException &cast_exception)
		{
			throw CreateConfigError("[%s.%s] Could not cast %s to %s", item_path.CStr(), child_name.CStr(), cast_exception.from.CStr(), cast_exception.to.CStr());
		}

		auto list_value_type = list_interface->GetListItemType();

		switch (list_value_type)
		{
			case ValueType::Unknown:
				OV_ASSERT2(false);
				break;

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
				break;

			case ValueType::Item:
				list_interface->ValidateOmitJsonNameRule(item_path, child->GetItemName());
				break;

			case ValueType::List:
				// Config module doesn't support nested list
				OV_ASSERT2(false);
				break;
		}
	}

	void Item::ValidateOmitJsonNameRule(const ov::String &item_path, const ov::String &name) const
	{
		RebuildListIfNeeded();

		auto child_path = item_path.IsEmpty() ? name : ov::String::FormatString("%s.%s", item_path.CStr(), name.CStr());

		// Check omit rule for children
		for (const auto &child : _children)
		{
			auto omit_json	= child->OmitJsonName();
			auto child_type = child->GetType();

			if (omit_json)
			{
				if (_children.size() != 1)
				{
					throw CreateConfigError("[%s] Could not omit item - it has multiple children", child_path.CStr());
				}

				if (child_type != ValueType::List)
				{
					throw CreateConfigError("[%s] Could not omit item - only list type can be omitted", child_path.CStr());
				}
			}

			switch (child_type)
			{
				case ValueType::Unknown:
					OV_ASSERT2(false);
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
					// Nothing to do
					OV_ASSERT2(omit_json == false);
					break;

				case ValueType::Item:
					OV_ASSERT2(omit_json == false);
					ValidateOmitJsonNameRuleForItem(child_path, child);
					break;

				case ValueType::List:
					ValidateOmitJsonNameRuleForList(child_path, child);
					break;
			}
		}
	}

	void Item::ValidateOmitJsonNameRules(const ov::String &item_path) const
	{
		ValidateOmitJsonNameRule(item_path, _item_name.GetName(DataType::Json));
	}

	void Item::ValidateOmitJsonNameRules() const
	{
		ValidateOmitJsonNameRules("");
	}

	ov::String Item::ToString(int indent_count) const
	{
		RebuildListIfNeeded();

		ov::String indent  = MakeIndentString(indent_count);

		auto item_name	   = _item_name.ToString();
		size_t child_count = _children.size();

		ov::String description;
		ov::String extra;

#if CFG_VERBOSE_STRING
		extra.Format(
			CFG_EXTRA_PREFIX
#	if CFG_PRINT_MEMORY_ADDRESS
			"%p: "
#	endif	// CFG_PRINT_MEMORY_ADDRESS
			"%s, "
			"children = %zu, "
			"%s, "
			"%s",
#	if CFG_PRINT_MEMORY_ADDRESS
			this,
#	endif	// CFG_PRINT_MEMORY_ADDRESS
			ov::Demangle(typeid(*this).name()).CStr(),
			child_count,
			_is_parsed ? "Parsed" : "NotParsed",
			_is_read_only ? "ReadOnly" : "Writable");
#endif	// CFG_VERBOSE_STRING

		description.AppendFormat(
			"%s"
			"%s: {%s"
			"%s" CFG_EXTRA_SUFFIX,
			indent.CStr(),
			item_name.CStr(), (child_count == 0) ? "}" : "",
			extra.CStr());

		size_t index = 0;

		for (const auto &child : _children)
		{
			description.AppendFormat("\n%s",
									 ChildToString(indent_count, child, index, child_count).CStr());

			index++;
		}

		if (child_count > 0)
		{
			description.AppendFormat("\n%s}", indent.CStr());
		}

		return description;
	}

	void Item::SetOriginalJsonValue(ValueType value_type, Json::Value &object, const ov::String &child_name, const Json::Value &original_value)
	{
		OV_ASSERT2(
			(value_type == ValueType::Unknown) ||
			(value_type == ValueType::String) ||
			(value_type == ValueType::Integer) ||
			(value_type == ValueType::Long) ||
			(value_type == ValueType::Boolean) ||
			(value_type == ValueType::Double) ||
			(value_type == ValueType::Attribute) ||
			(value_type == ValueType::Text));

		if (object.isNull())
		{
			// Convert `object` to an object
			object = Json::objectValue;
		}

		const auto &value = (value_type != ValueType::Unknown) ? original_value : Json::nullValue;

		if (object.isArray())
		{
			if (value_type != ValueType::Attribute)
			{
				object.append(value);
			}
			else
			{
				Json::Value attribute_object = Json::objectValue;
				attribute_object["$"]		 = value;
				object.append(attribute_object);
			}
		}
		else if (object.isObject())
		{
			auto &target_object = (value_type != ValueType::Attribute) ? object : GetJsonObject(object, "$");

			if ((target_object.isNull() == false) && (target_object.isObject() == false))
			{
				OV_ASSERT(false, "Expected JSON object for attribute, but got: %s", ov::StringFromJsonValueType(target_object));
				target_object = Json::objectValue;
			}

			target_object[child_name] = value;
		}
		else
		{
			throw CreateConfigError("Invalid JSON object type: %s", child_name.CStr());
		}
	}

	void Item::SetCurrentJsonValue(ValueType value_type, Json::Value &object, const ov::String &child_name, const Variant &member)
	{
		Json::Value member_value;

		switch (value_type)
		{
			case ValueType::Unknown:
				// Unknown type is considered as null.
				member_value = Json::nullValue;
				break;

			case ValueType::Item:
				OV_ASSERT2(false);
				return;

			case ValueType::List:
				OV_ASSERT2(false);
				return;

			case ValueType::String:
				member_value = member.TryCast<ov::String *>()->CStr();
				break;

			case ValueType::Integer:
				member_value = *(member.TryCast<int *>());
				break;

			case ValueType::Long:
				member_value = *(member.TryCast<long *>());
				break;

			case ValueType::Boolean:
				member_value = *(member.TryCast<bool *>());
				break;

			case ValueType::Double:
				member_value = *(member.TryCast<double *>());
				break;

			case ValueType::Attribute:
				member_value = member.TryCast<Attribute *>()->GetValue().CStr();
				break;

			case ValueType::Text:
				member_value = member.TryCast<Text *>()->ToString().CStr();
				break;
		}

		if (object.isNull())
		{
			// Convert `object` to an object
			object = Json::objectValue;
		}

		if (object.isArray())
		{
			if (value_type != ValueType::Attribute)
			{
				// If the value type is not an attribute, we can append the value to the array.
				object.append(member_value);
			}
			else
			{
				Json::Value attribute_object = Json::objectValue;
				attribute_object["$"]		 = member_value;
				object.append(attribute_object);
			}
		}
		else if (object.isObject())
		{
			auto &target_object = (value_type != ValueType::Attribute) ? object : GetJsonObject(object, "$");

			if ((target_object.isNull() == false) && (target_object.isObject() == false))
			{
				OV_ASSERT(false, "Expected JSON object for attribute, but got: %s", ov::StringFromJsonValueType(target_object));
				target_object = Json::objectValue;
			}

			target_object[child_name] = member_value;
		}
		else
		{
			throw CreateConfigError("Invalid JSON object type: %s", child_name.CStr());
		}
	}

	void Item::ToJson(Json::Value &value, bool original_value, bool include_default_values) const
	{
		RebuildListIfNeeded();

		for (const auto &child : _children)
		{
			if (include_default_values || child->IsParsed())
			{
				auto value_type	 = child->GetType();

				auto &child_name = child->GetItemName().GetName(DataType::Json);

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
						if (original_value)
						{
							SetOriginalJsonValue(value_type, value, child_name, child->GetOriginalValue());
						}
						else
						{
							SetCurrentJsonValue(value_type, value, child_name, child->GetMemberPointer());
						}
						break;

					case ValueType::Item: {
						Item *child_item = nullptr;

						try
						{
							child_item = child->GetMemberPointerAs<Item>();
						}
						catch (const CastException &cast_exception)
						{
							HANDLE_CAST_EXCEPTION(value_type, );
						}

						auto &target_value = child->OmitJsonName() ? value : GetJsonObject(value, child_name);

						if (target_value.isNull())
						{
							target_value = Json::objectValue;
						}

						child_item->ToJson(target_value, original_value, include_default_values);

						break;
					}

					case ValueType::List: {
						std::shared_ptr<ListInterface> list_item;

						try
						{
							list_item = std::dynamic_pointer_cast<ListInterface>(child);
						}
						catch (const CastException &cast_exception)
						{
							HANDLE_CAST_EXCEPTION(value_type, );
						}

						auto &target_value = child->OmitJsonName() ? value : GetJsonObject(value, child_name);

						if (target_value.isArray() == false)
						{
							target_value = Json::arrayValue;
						}

						list_item->CopyToJsonValue(target_value, original_value, include_default_values);

						break;
					}
				}
			}
		}
	}

	void SetChildValueToXmlNode(pugi::xml_node &node, const ov::String &child_name, const char *value)
	{
		auto child = node.append_child((value != nullptr) ? pugi::node_element : pugi::node_null);
		child.set_name(child_name.CStr());

		if (value != nullptr)
		{
			child.text().set(value);
		}
	}

	void Item::ToXml(pugi::xml_node &node, bool include_default_values) const
	{
		if (node.set_name(_item_name.xml_name.CStr()) == false)
		{
			return;
		}

		RebuildListIfNeeded();

		for (const auto &child : _children)
		{
			if (include_default_values || child->IsParsed())
			{
				auto value_type		 = child->GetType();

				auto &child_name	 = child->GetItemName().GetName(DataType::Xml);
				auto &original_value = child->GetOriginalValue();

				switch (value_type)
				{
					case ValueType::Unknown:
						SetChildValueToXmlNode(node, child_name, nullptr);
						break;

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
					case ValueType::Text:
						SetChildValueToXmlNode(node, child_name, ov::Converter::ToString(original_value));
						break;

					case ValueType::Attribute:
						node.append_attribute(child_name).set_value(ov::Converter::ToString(original_value));
						break;

					case ValueType::Item: {
						Item *child_item = nullptr;

						try
						{
							child_item = child->GetMemberPointerAs<Item>();
						}
						catch (const CastException &cast_exception)
						{
							HANDLE_CAST_EXCEPTION(value_type, );
						}

						auto child = node.append_child(child_name);
						child_item->ToXml(child, include_default_values);

						break;
					}

					case ValueType::List: {
						std::shared_ptr<ListInterface> list_item;

						try
						{
							list_item = std::dynamic_pointer_cast<ListInterface>(child);
						}
						catch (const CastException &cast_exception)
						{
							HANDLE_CAST_EXCEPTION(value_type, );
						}

						list_item->CopyToXmlNode(node, include_default_values);

						break;
					}
				}
			}
		}
	}

	void Item::FromDataSourceInternal(ov::String item_path, const DataSource &data_source, bool allow_optional)
	{
		if (_last_target != this)
		{
			throw CreateConfigError(
				"Rebuilding of children list is required before call FromDataSource()");
		}

		ov::String pattern;
		std::vector<ov::String> include_files;

		if (data_source.GetIncludeFileList(&pattern, &include_files))
		{
			auto current_path = data_source.GetCurrentPath();

			if (include_files.empty())
			{
				// "include" attribute is present, but there is no file to include
				throw CreateConfigError(
					"There is no file to include for path: %s, current path: %s, include pattern: %s",
					item_path.CStr(),
					current_path.CStr(), pattern.CStr());
			}
			else
			{
				if (include_files.size() > 1)
				{
					throw CreateConfigError(
						"Too many files found for an Item: %s, current path: %s, include pattern: %s (%zu files found)",
						item_path.CStr(),
						current_path.CStr(), pattern.CStr(), include_files.size());
				}

				auto &include_file	 = include_files[0];
				auto new_data_source = data_source.NewDataSource(include_file, _item_name);
				FromDataSourceInternal(item_path, new_data_source, allow_optional);
			}

			return;
		}
		else
		{
			// "include" attribute is not present
		}

		for (auto &child : _children)
		{
			child->SetValue(item_path, data_source, allow_optional || child->IsOptional());
		}

		_is_parsed = true;
	}
}  // namespace cfg
