//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2021 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <map>
#include <pugixml-1.9/src/pugixml.hpp>

#include "./item_name.h"
#include "./variant.h"

namespace cfg
{
	class DataSource
	{
	public:
		DataSource(const ov::String &current_path, const ov::String &file_name, const std::shared_ptr<pugi::xml_document> &document, const pugi::xml_node &node, CheckUnknownItems check_unknown_items = CheckUnknownItems::Check);
		DataSource(const ov::String &current_path, const ov::String &file_name, const ov::String json_name, const Json::Value &json, CheckUnknownItems check_unknown_items = CheckUnknownItems::Check);
		// @param type XML/JSON
		// @param current_path cwd
		// @param file_name config file name
		// @param root_name The name of root element
		MAY_THROWS(cfg::ConfigError)
		DataSource(DataType type, const ov::String &current_path, const ov::String &file_name, const ItemName &root_name, CheckUnknownItems check_unknown_items = CheckUnknownItems::Check);
		// @param type XML/JSON
		// @param file_path directory + file_name (current_path + file_name)
		// @param root_name The name of root element
		MAY_THROWS(cfg::ConfigError)
		DataSource(DataType type, const ov::String &file_path, const ItemName &root_name, CheckUnknownItems check_unknown_items = CheckUnknownItems::Check);

		DataType GetType() const
		{
			return _type;
		}

		const ov::String &ResolveName(const ItemName &name) const
		{
			return name.GetName(_type);
		}

		bool IsSourceOf(const ItemName &name) const
		{
			switch (_type)
			{
				case DataType::Xml:
					return name.xml_name == _node.name();

				case DataType::Json:
					return name.json_name == _json_name;
			}

			return false;
		}

		MAY_THROWS(cfg::ConfigError)
		void CheckUnknownItems(const ov::String &path,
							   const std::unordered_map<ov::String, std::shared_ptr<Child>> &children_for_xml,
							   const std::unordered_map<ov::String, std::shared_ptr<Child>> &children_for_json) const;

		// Check weather the root value is array or not
		bool IsArray(const ItemName &name) const;

		MAY_THROWS(cfg::ConfigError)
		Variant GetRootValue(ValueType value_type, bool resolve_path, bool omit_json, Json::Value *original_value) const;
		MAY_THROWS(cfg::ConfigError)
		Variant GetValue(ValueType value_type, const ItemName &name, bool resolve_path, bool omit_json, Json::Value *original_value) const;

		// Create a data source from this context
		DataSource NewDataSource(const ov::String &file_name, const ItemName &root_name) const
		{
			DataSource new_data_source(_type, _current_file_path, file_name, root_name, _check_unknown_items);

			return new_data_source;
		}

		ov::String GetCurrentPath() const
		{
			return _current_file_path;
		}

		ov::String GetFileName() const
		{
			return _file_name;
		}

		ov::String GetFullPath() const
		{
			return _full_file_path;
		}

		bool GetIncludeFileList(ov::String *pattern, std::vector<ov::String> *include_file_list) const;

		ov::String ToString() const;

	protected:
		void LoadFromFile(ov::String file_name, const ItemName &root_name);

		void LoadFromXmlFile(const ov::String &file_name, const ov::String &root_name);
		void LoadFromJson(const ov::String &file_name, const ov::String &root_name);

		MAY_THROWS(cfg::ConfigError)
		Variant GetValueFromXml(ValueType value_type, const ov::String &name, bool is_child, bool resolve_path, Json::Value *original_value) const;
		MAY_THROWS(cfg::ConfigError)
		Variant GetValueFromJson(ValueType value_type, const ov::String &name, bool is_child, bool resolve_path, bool omit_json, Json::Value *original_value) const;

	protected:
		DataType _type;

		cfg::CheckUnknownItems _check_unknown_items = CheckUnknownItems::Check;

		std::shared_ptr<pugi::xml_document> _document;
		pugi::xml_node _node;

		ov::String _json_name;
		Json::Value _json;

		// _full_file_path = _current_file_path + _file_name
		ov::String _full_file_path;
		ov::String _current_file_path;
		ov::String _file_name;
	};
}  // namespace cfg
