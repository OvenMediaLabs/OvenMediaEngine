//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2021 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <base/ovlibrary/ovlibrary.h>

#include <pugixml-1.9/src/pugixml.hpp>

#include "./annotations.h"
#include "./child.h"
#include "./config_error.h"
#include "./data_source.h"

namespace cfg
{
	class Item;

	class ListInterface : public Child
	{
		friend class Item;

	public:
		ListInterface(const ItemName &item_name, const ov::String &type_name,
					  Optional is_optional, cfg::ResolvePath resolve_path, cfg::OmitJsonName omit_json_name,
					  OptionalCallback optional_callback, ValidationCallback validation_callback,
					  void *member_raw_pointer, std::any member_pointer, ValueType list_item_type);

		void AddChildValueList(const std::vector<DataSource> &data_source_list);

		virtual void CopyFrom(const std::shared_ptr<const ListInterface> &another_list);

		MAY_THROWS(cfg::ConfigError)
		virtual void CopyToXmlNode(pugi::xml_node &node, bool include_default_values) const = 0;

		MAY_THROWS(cfg::ConfigError)
		// virtual void AddChildrenToJson(Json::Value &object, ValueType value_type, OmitRule omit_name, const ov::String &child_name, const std::any &child_target, const Json::Value &original_value, bool include_default_values) const = 0;
		virtual void CopyToJsonValue(Json::Value &object, bool original_value, bool include_default_values) const = 0;

		virtual ov::String ToString(int indent_count, const std::shared_ptr<const Child> &child) const			  = 0;

		virtual size_t GetCount() const																			  = 0;
		virtual void Clear()																					  = 0;

		ValueType GetListItemType() const
		{
			return _list_item_type;
		}

		// Get type name of Ttype
		virtual ov::String GetListItemTypeName() const = 0;

	protected:
		static void SetJsonChildValue(ValueType value_type, Json::Value &object, const ov::String &child_name, const Json::Value &original_value);
		static void SetJsonChildValue(ValueType value_type, Json::Value &object, const ov::String &child_name, const ov::String &original_value)
		{
			SetJsonChildValue(value_type, object, child_name, Json::Value(original_value.CStr()));
		}
		static void SetJsonChildValue(ValueType value_type, Json::Value &object, const ov::String &child_name, const Item &original_value)
		{
			// This function is declared for the specialization of this API and is not actually called
			OV_ASSERT2(false);
		}

		MAY_THROWS(cfg::CastException)
		virtual void AppendChildValue(const DataSource &data_source, ValueType list_item_type, bool resolve_path, bool omit_json) = 0;

		MAY_THROWS(cfg::ConfigError)
		void ValidateOmitJsonNameRule(const ov::String &path, const ItemName &item_name) const;

		MAY_THROWS(cfg::ConfigError)
		virtual void ValidateOmitJsonNameRuleForItem(const ov::String &item_path, const ItemName &item_name) const = 0;

		MAY_THROWS(cfg::ConfigError)
		void ValidateOmitJsonNameRuleInternal(const ov::String &item_path, const ItemName &item_name) const
		{
			switch (GetListItemType())
			{
				case ValueType::Item: {
					ValidateOmitJsonNameRuleForItem(item_path, item_name);
					break;
				}

				case ValueType::List: {
					// Config module doesn't support nested list
					throw CreateConfigError("Config module doesn't support nested list");
				}

				default:
					// Do not need to validate omit rule for other types
					break;
			}
		}

		ValueType _list_item_type;

		// This is only valid when _list_item_type is primitive type
		std::vector<Json::Value> _original_value_list;
	};
}  // namespace cfg
