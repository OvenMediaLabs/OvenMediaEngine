//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2022 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <base/ovlibrary/ovlibrary.h>

#include <any>
#include <functional>

#include "./list_interface.h"
#include "./text.h"
#include "./value_type.h"

namespace cfg
{
	//--------------------------------------------------------------------
	// CopyValueToXmlNode()
	//--------------------------------------------------------------------
	MAY_THROWS(cfg::ConfigError)
	template <typename Tvalue_type, std::enable_if_t<!std::is_base_of_v<Item, Tvalue_type>, int> = 0>
	void CopyValueToXmlNode(pugi::xml_node &node, const ov::String &item_name, const Tvalue_type *value, bool include_default_values)
	{
		node.text().set(ToString(value));
	}

	MAY_THROWS(cfg::ConfigError)
	void CopyValueToXmlNode(pugi::xml_node &node, const ov::String &item_name, const Json::Value &value, bool include_default_values);

	MAY_THROWS(cfg::ConfigError)
	void CopyValueToXmlNode(pugi::xml_node &node, const ov::String &item_name, const Item *value, bool include_default_values);
	//--------------------------------------------------------------------

	//--------------------------------------------------------------------
	// CopyValueToJson()
	//--------------------------------------------------------------------
	MAY_THROWS(cfg::ConfigError)
	template <typename Tvalue_type, std::enable_if_t<!std::is_base_of_v<Item, Tvalue_type>, int> = 0>
	void CopyValueToJson(Json::Value &json, const Tvalue_type *value, bool original_value, bool include_default_values)
	{
		// This function is declared for the specialization of this API and is not actually called
		OV_ASSERT2(false);
	}

	MAY_THROWS(cfg::ConfigError)
	void CopyValueToJson(Json::Value &json, const Item *value, bool original_value, bool include_default_values);
	//--------------------------------------------------------------------

	void SetItemDataFromDataSource(Item *item, const ItemName &item_name, const DataSource &data_source);

	template <typename Ttype>
	class List : public ListInterface
	{
	public:
		using Tlist_item = Ttype;
		using ListType	 = std::vector<Tlist_item>;

		List(const ItemName &item_name, const ov::String &type_name,
			 Optional is_optional, cfg::ResolvePath resolve_path, cfg::OmitJsonName omit_json_name,
			 OptionalCallback optional_callback, ValidationCallback validation_callback,
			 ListType *item_list)
			: ListInterface(
				  item_name, type_name,
				  is_optional, resolve_path, omit_json_name,
				  optional_callback, validation_callback,
				  item_list, item_list, ProbeType<Tlist_item>::type),
			  _item_list(item_list)
		{
		}

		template <typename Titem_type = Tlist_item, std::enable_if_t<!std::is_base_of_v<Item, Titem_type>, int> = 0>
		void Push(Ttype value)
		{
			SetParsed(true);

			_item_list->emplace_back(std::move(value));
		}

		template <typename Titem_type = Tlist_item, std::enable_if_t<std::is_base_of_v<Item, Titem_type>, int> = 0>
		void Push(Ttype value)
		{
			SetParsed(true);

			value.SetParsed(true);
			value.SetItemName(_item_name);

			_item_list->emplace_back(std::move(value));
		}

		void RemoveIf(const std::function<bool(const Tlist_item &)> &callback)
		{
			for (auto item_iterator = _item_list->begin(); item_iterator != _item_list->end();)
			{
				if (callback(*item_iterator))
				{
					item_iterator = _item_list->erase(item_iterator);
				}
				else
				{
					++item_iterator;
				}
			}
		}

		void CopyFrom(const std::shared_ptr<const ListInterface> &another_list) override
		{
#if DEBUG
			auto from_list = std::dynamic_pointer_cast<const List<Tlist_item>>(another_list);
#else	// DEBUG
			auto from_list = std::static_pointer_cast<const List<Tlist_item>>(another_list);
#endif	// DEBUG

			if (from_list == nullptr)
			{
				OV_ASSERT2(false);
				return;
			}

			// Copy items
			ListInterface::CopyFrom(another_list);

			const auto &another_item_list = *(from_list->_item_list);

			*_item_list					  = another_item_list;
		}

		ov::String GetListItemTypeName() const override
		{
			return ov::Demangle(typeid(Tlist_item).name());
		}

		// Copy children to xml_node
		MAY_THROWS(cfg::ConfigError)
		void CopyToXmlNode(pugi::xml_node &node, bool include_default_values) const override
		{
			const auto &list_target = *_item_list;

			if (list_target.empty())
			{
				return;
			}

			auto list_item_type = GetListItemType();
			auto item_name		= _item_name.GetName(DataType::Xml);

			if (list_item_type == ValueType::Item)
			{
				for (const auto &list_item : list_target)
				{
					auto new_child = node.append_child(item_name);
					CopyValueToXmlNode(new_child, item_name, &list_item, include_default_values);
				}
			}
			else
			{
				auto list_item = list_target.begin();

				if (list_target.size() < _original_value_list.size())
				{
					OV_ASSERT2(false);
					return;
				}

				// Copy original values (these data generated from XML/JSON data sources)
				for ([[maybe_unused]] const auto &original_value : _original_value_list)
				{
					auto new_child = node.append_child(item_name);
					CopyValueToXmlNode(new_child, item_name, original_value, include_default_values);

					list_item++;
				}

				// Copy rest items (dynamic data, these data have appended programmatically)
				while (list_item != list_target.end())
				{
					auto item	   = *list_item;
					auto new_child = node.append_child(item_name);
					CopyValueToXmlNode(new_child, item_name, &item, include_default_values);

					list_item++;
				}
			}
		}

		// Copy children to json array
		MAY_THROWS(cfg::ConfigError)
		void CopyToJsonValue(Json::Value &value, bool original_value, bool include_default_values) const override
		{
			const auto &list_target = *_item_list;

			if (list_target.empty())
			{
				return;
			}

			if (value.isArray() == false)
			{
				OV_ASSERT2(false);
				value = Json::arrayValue;
			}

			auto list_item_type = GetListItemType();
			auto child_name		= GetItemName().GetName(DataType::Json);

			if (list_item_type == ValueType::Item)
			{
				for (const auto &list_item : list_target)
				{
					CopyValueToJson(value.append({}), &list_item, original_value, include_default_values);
				}
			}
			else
			{
				auto list_item = list_target.begin();

				if (list_target.size() < _original_value_list.size())
				{
					OV_ASSERT2(false);
					return;
				}

				// Copy original values (these data generated from XML/JSON data sources)
				for (const auto &original_value : _original_value_list)
				{
					SetJsonChildValue(list_item_type, value, child_name, original_value);

					list_item++;
				}

				// Copy rest items (dynamic data, these data have appended programmatically)
				while (list_item != list_target.end())
				{
					SetJsonChildValue(list_item_type, value, child_name, *list_item);

					list_item++;
				}
			}
		}

		size_t GetCount() const override
		{
			return _item_list->size();
		}

		void Clear() override
		{
			_item_list->clear();
		}

		ov::String ToString(int indent_count, const std::shared_ptr<const Child> &list_info) const override
		{
			if (_item_list->empty())
			{
				return {};
			}

			const auto &list_target = *_item_list;
			size_t index			= 0;
			ov::String indent		= MakeIndentString(indent_count);
			ov::String description;
			bool is_item = (GetListItemType() == ValueType::Item);

			for (const auto &list_item : list_target)
			{
				if (index > 0)
				{
					description.Append(",\n");
				}

				if (is_item)
				{
					description.Append(ToDebugString(indent_count, &list_item).CStr());
				}
				else
				{
					description.AppendFormat(
						"%s"
						"%zu: %s",
						indent.CStr(),
						index, ToDebugString(indent_count + 1, &list_item).CStr());
				}

				index++;
			}

			description.Append("\n");

			return description;
		}

	protected:
		MAY_THROWS(cfg::CastException)
		template <typename Titem_type = Tlist_item, std::enable_if_t<!std::is_base_of_v<Item, Titem_type>, int> = 0>
		void SetValue(const DataSource &data_source, ValueType list_item_type, bool resolve_path, bool omit_json)
		{
			Json::Value original_value;
			auto value = data_source.GetRootValue(list_item_type, resolve_path, omit_json, &original_value);

			if (value.HasValue() == false)
			{
				// The data source doesn't contain <name> elements (It contains another XML/JSON elements)
				return;
			}

			_item_list->emplace_back(value.TryCast<Tlist_item>());
			_original_value_list.push_back(std::move(original_value));
		}

		MAY_THROWS(cfg::ConfigError)
		template <typename Titem_type = Tlist_item, std::enable_if_t<std::is_base_of_v<Item, Titem_type>, int> = 0>
		void SetValue(const DataSource &data_source, ValueType list_item_type, bool resolve_path, bool omit_json)
		{
			Tlist_item list_item;

			SetItemDataFromDataSource(&list_item, _item_name, data_source);

			_item_list->push_back(std::move(list_item));
		}

		MAY_THROWS(cfg::CastException)
		MAY_THROWS(cfg::ConfigError)
		void AppendChildValue(const DataSource &data_source, ValueType list_item_type, bool resolve_path, bool omit_json) override
		{
			SetValue(data_source, list_item_type, resolve_path, omit_json);
		}

		template <typename Titem_type = Tlist_item, std::enable_if_t<!std::is_base_of_v<Item, Titem_type>, int> = 0>
		void ValidateOmitJsonNameRule(const ov::String &item_path, const ItemName &item_name) const
		{
			OV_ASSERT2(false);
		}

		MAY_THROWS(cfg::ConfigError)
		template <typename Titem_type = Tlist_item, std::enable_if_t<std::is_base_of_v<Item, Titem_type>, int> = 0>
		void ValidateOmitJsonNameRule(const ov::String &item_path, const ItemName &item_name) const
		{
			Tlist_item item;

			item.SetItemName(item_name);
			item.ValidateOmitJsonNameRules(item_path);
		}

		MAY_THROWS(cfg::ConfigError)
		void ValidateOmitJsonNameRuleForItem(const ov::String &item_path, const ItemName &item_name) const override
		{
			return ValidateOmitJsonNameRule(item_path, item_name);
		}

		ListType *_item_list;
	};

}  // namespace cfg
