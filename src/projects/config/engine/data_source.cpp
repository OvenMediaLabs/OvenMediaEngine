//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2021 AirenSoft. All rights reserved.
//
//==============================================================================
#include "data_source.h"

#include <unistd.h>

#include <fstream>
#include <regex>

#include "./annotations.h"
#include "./config_error.h"
#include "./item.h"
#include "./variant.h"

#define OV_LOG_TAG "Config.DataSource"

namespace cfg
{
	struct XmlWriter : pugi::xml_writer
	{
		ov::String result;

		void write(const void *data, size_t size) override
		{
			result.Append(static_cast<const char *>(data), size);
		}
	};

	ov::String GetEnv(const char *key, const char *default_value, bool *is_default_value)
	{
		auto env = std::getenv(key);

		if (env != nullptr)
		{
			return env;
		}

		if (::strcmp(key, "HOSTNAME") == 0)
		{
			ov::String hostname;

			if (hostname.SetCapacity(HOST_NAME_MAX))
			{
				if (::gethostname(hostname.GetBuffer(), hostname.GetCapacity()) == 0)
				{
					return hostname;
				}
			}
		}

		if ((default_value != nullptr) && (default_value[0] != '\0'))
		{
			// Use the default value
			if (is_default_value != nullptr)
			{
				*is_default_value = true;
			}

			return default_value;
		}

		// There is no default value
		return "";
	}

	// Preprocess for ${env:XXX} macro
	ov::String PreprocessForEnv(const ov::String &value)
	{
		std::string str = value.CStr();
		std::string result;
		std::regex r(R"(\$\{env:([^}]*)\})");

		auto start					  = str.cbegin();

		std::sregex_iterator iterator = std::sregex_iterator(start, str.cend(), r);

		while (iterator != std::sregex_iterator())
		{
			std::smatch matches = *iterator;
			auto match			= matches[0];
			auto token			= matches[1];

			auto env_key		= token.str();
			auto position		= env_key.find_first_of(':');

			std::string default_value;
			bool is_default_value = false;

			if (position != std::string::npos)
			{
				default_value = env_key.substr(position + 1);
				env_key		  = env_key.substr(0, position);
			}

			auto key		 = env_key.c_str();
			ov::String value = GetEnv(key, default_value.c_str(), &is_default_value);

			result.append(start, match.first);
			result.append(value);

			start = match.second;

			++iterator;
		}

		result.append(start, str.cend());

		return result.c_str();
	}

	ov::String PreprocessForMacros(ov::String str)
	{
		return str
			.Replace("${ome:Home}", ov::PathManager::GetAppPath())
			.Replace("${ome:CurrentPath}", ov::PathManager::GetCurrentPath());
	}

	// Preprocess for ResolvePath annotation
	ov::String PreprocessForPath(const ov::String &current_path, ov::String str)
	{
		if (ov::PathManager::IsAbsolute(str) == false)
		{
			// relative path
			return ov::PathManager::Combine(current_path, str);
		}

		return str;
	}

	ov::String Preprocess(const ov::String &current_path, const ov::String &value, bool resolve_path)
	{
		ov::String result = PreprocessForEnv(value);

		result			  = PreprocessForMacros(result);

		if (resolve_path)
		{
			result = PreprocessForPath(current_path, result);
		}

		return result;
	}

#define SET_ORIGINAL_VALUE_IF_NOT_NULL(value) \
	if (original_value != nullptr)            \
	{                                         \
		*original_value = value;              \
	}

	Variant GetAttribute(
		const ov::String &current_file_path,
		const pugi::xml_node &node, const char *name,
		const bool resolve_path)
	{
		const auto &attribute = node.attribute(name);
		return attribute.empty() ? Variant() : Preprocess(current_file_path, attribute.value(), resolve_path);
	}

	bool NeedToIgnore(
		const ov::String &current_file_path,
		const pugi::xml_node &node,
		const bool resolve_path)
	{
		const auto ignore = GetAttribute(current_file_path, node, "ignore", false);

		if (ignore.HasValue())
		{
			try
			{
				// Ignore if the value is exactly "true"
				if (ignore.TryCast<ov::String>() == "true")
				{
					return true;
				}

				// Otherwise, use the value as-is
			}
			catch (const CastException &cast_exception)
			{
			}
		}

		return false;
	}

	Variant ProcessNode(
		const ov::String &current_file_path,
		const pugi::xml_node &node,
		const std::function<Variant(const ov::String &value)> converter,
		const bool resolve_path,
		Json::Value *original_value)
	{
		if (NeedToIgnore(current_file_path, node, resolve_path))
		{
			return {};
		}

		if (node.empty())
		{
			// Nothing to do
			*original_value = Json::nullValue;
			return {};
		}

		const auto &child_value = node.child_value();
		SET_ORIGINAL_VALUE_IF_NOT_NULL(child_value);

		auto preprocessed = Preprocess(current_file_path, child_value, resolve_path);

		return (converter != nullptr) ? converter(preprocessed) : preprocessed;
	}

	Variant ProcessNodes(
		const ov::String &current_file_path,
		const pugi::xml_object_range<pugi::xml_named_node_iterator> &nodes,
		const std::function<Variant(const ov::String &value)> converter,
		const bool resolve_path,
		Json::Value *original_value)
	{
		for (auto &node : nodes)
		{
			if (NeedToIgnore(current_file_path, node, resolve_path))
			{
				continue;
			}

			if (node.empty())
			{
				// Nothing to do
				*original_value = Json::nullValue;
				break;
			}

			const auto &child_value = node.child_value();
			SET_ORIGINAL_VALUE_IF_NOT_NULL(child_value);

			auto preprocessed = Preprocess(current_file_path, child_value, resolve_path);

			return (converter != nullptr) ? converter(preprocessed) : preprocessed;
		}

		return {};
	}

	Variant Process(
		const ov::String &current_file_path,
		const bool is_child,
		const pugi::xml_node &node,
		const ov::String &name,
		const std::function<Variant(const ov::String &value)> converter,
		const bool resolve_path,
		Json::Value *original_value)
	{
		return is_child
				   ? ProcessNodes(current_file_path, node.children(name), converter, resolve_path, original_value)
				   : ProcessNode(current_file_path, node, converter, resolve_path, original_value);
	}

	Variant DataSource::GetValueFromXml(ValueType value_type, const ov::String &name, bool is_child, bool resolve_path, Json::Value *original_value) const
	{
		switch (value_type)
		{
			case ValueType::Unknown:
				SET_ORIGINAL_VALUE_IF_NOT_NULL(Json::nullValue);
				return {};

			case ValueType::String:
				return Process(_current_file_path, is_child, _node, name, nullptr, resolve_path, original_value);

			case ValueType::Integer:
				return Process(_current_file_path, is_child, _node, name, [](const ov::String &value) { return ov::Converter::ToInt32(value); }, resolve_path, original_value);

			case ValueType::Long:
				return Process(_current_file_path, is_child, _node, name, [](const ov::String &value) { return ov::Converter::ToInt64(value); }, resolve_path, original_value);

			case ValueType::Boolean:
				return Process(_current_file_path, is_child, _node, name, [](const ov::String &value) { return ov::Converter::ToBool(value); }, resolve_path, original_value);

			case ValueType::Double:
				return Process(_current_file_path, is_child, _node, name, [](const ov::String &value) { return ov::Converter::ToDouble(value); }, resolve_path, original_value);

			case ValueType::Attribute: {
				if (NeedToIgnore(_current_file_path, _node, resolve_path) == false)
				{
					const auto &value = GetAttribute(_current_file_path, _node, name, resolve_path);

					if (value.HasValue())
					{
						SET_ORIGINAL_VALUE_IF_NOT_NULL(value.TryCast<ov::String>().CStr());
					}

					return value;
				}

				SET_ORIGINAL_VALUE_IF_NOT_NULL(Json::nullValue);
				return {};
			}

			case ValueType::Text:
				return Process(_current_file_path, is_child, _node, name, nullptr, resolve_path, original_value);

			case ValueType::Item: {
				if (
					(is_child == false) &&
					(_node.empty() == false) &&
					(NeedToIgnore(_current_file_path, _node, resolve_path) == false))
				{
					SET_ORIGINAL_VALUE_IF_NOT_NULL(Json::objectValue);
					return DataSource(_current_file_path, _file_name, _document, _node, _check_unknown_items);
				}

				for (auto &child_node : _node.children(name))
				{
					if ((child_node.empty() == false) && (NeedToIgnore(_current_file_path, child_node, resolve_path) == false))
					{
						SET_ORIGINAL_VALUE_IF_NOT_NULL(Json::objectValue);
						return DataSource(_current_file_path, _file_name, _document, child_node, _check_unknown_items);
					}
				}

				SET_ORIGINAL_VALUE_IF_NOT_NULL(Json::nullValue);
				return {};
			}

			case ValueType::List: {
				if (_node.empty() == false)
				{
					std::vector<DataSource> data_sources;

					SET_ORIGINAL_VALUE_IF_NOT_NULL(Json::arrayValue);

					const auto children = _node.children(name);

					for (const auto &node_child : children)
					{
						if (NeedToIgnore(_current_file_path, node_child, resolve_path) == false)
						{
							data_sources.emplace_back(_current_file_path, _file_name, _document, node_child, _check_unknown_items);

							if (original_value != nullptr)
							{
								original_value->append(node_child.child_value());
							}
						}
					}

					if (data_sources.size() > 0)
					{
						return data_sources;
					}
				}

				SET_ORIGINAL_VALUE_IF_NOT_NULL(Json::nullValue);
				return {};
			}
		}

		return {};
	}

	Json::Value GetJsonValue(const Json::Value &value, const ov::String &name)
	{
		if (value.isObject())
		{
			if (value.isMember(name))
			{
				return value[name];
			}
		}

		return Json::nullValue;
	}

	Json::Value GetJsonAttribute(const Json::Value &value, const ov::String &attribute_name)
	{
		if (value.isObject() && value.isMember("$"))
		{
			return GetJsonValue(value["$"], attribute_name);
		}

		return Json::nullValue;
	}

	Variant GetJsonList(const ov::String &current_path, const ov::String &file_name, const Json::Value &json, const ov::String &name, bool omit_json, Json::Value *original_value, CheckUnknownItems check_unknown_items)
	{
		// json:
		//
		// {
		//     "items": [ <item1>, <item2>, ... ]
		// }
		//
		// or
		//
		// {
		//     "items": {
		//         "item": [ <item1>, <item2>, ... ]
		//     }
		// }
		Json::Value value_list;

		if (json.isArray())
		{
			value_list = json;

			if (omit_json == false)
			{
				if (current_path.IsEmpty())
				{
					throw CreateConfigError("%s is not an array", name.CStr());
				}
				else
				{
					throw CreateConfigError("%s is not an array (%s)", name.CStr(), current_path.CStr());
				}
			}
		}
		else
		{
			// Extract the value of the child node
			value_list = GetJsonValue(json, name);

			if (value_list.isNull())
			{
				return {};
			}

			if (value_list.isArray() == false)
			{
				if (current_path.IsEmpty())
				{
					throw CreateConfigError("Child %s is not an array", name.CStr());
				}
				else
				{
					throw CreateConfigError("Child %s is not an array (%s)", name.CStr(), current_path.CStr());
				}
			}
		}

		std::vector<DataSource> data_sources;

		for (const auto &json_value : value_list)
		{
			data_sources.emplace_back(current_path, file_name, name, json_value, check_unknown_items);
		}

		if (original_value != nullptr)
		{
			*original_value = std::move(value_list);
		}

		return data_sources;
	}

	DataSource::DataSource(const ov::String &current_path, const ov::String &file_name, const std::shared_ptr<pugi::xml_document> &document, const pugi::xml_node &node, cfg::CheckUnknownItems check_unknown_items)
		: _type(DataType::Xml),

		  _check_unknown_items(check_unknown_items),

		  _document(document),
		  _node(node),
		  _current_file_path(current_path),
		  _file_name(file_name)
	{
		logtt("Trying to create a DataSource from XML node [%s]... (%s, cwd: %s)", node.name(), file_name.CStr(), current_path.CStr());
	}

	DataSource::DataSource(const ov::String &current_path, const ov::String &file_name, const ov::String json_name, const Json::Value &json, cfg::CheckUnknownItems check_unknown_items)
		: _type(DataType::Json),

		  _check_unknown_items(check_unknown_items),

		  _json_name(json_name),
		  _json(json),
		  _current_file_path(current_path),
		  _file_name(file_name)
	{
		logtt("Trying to create a DataSource from JSON value [%s]... (%s, cwd: %s)", json_name.CStr(), file_name.CStr(), current_path.CStr());
	}

	DataSource::DataSource(DataType type, const ov::String &current_path, const ov::String &file_name, const ItemName &root_name, cfg::CheckUnknownItems check_unknown_items)
		: _type(type),

		  _check_unknown_items(check_unknown_items),

		  _current_file_path(current_path)
	{
		_full_file_path = file_name;

		if (ov::PathManager::IsAbsolute(_full_file_path))
		{
			_current_file_path.Clear();
		}
		else
		{
			if (_current_file_path.IsEmpty() == false)
			{
				_full_file_path = ov::PathManager::Combine(_current_file_path, _full_file_path);
			}
		}

		if (_current_file_path.IsEmpty())
		{
			_current_file_path = ov::PathManager::ExtractPath(_full_file_path);
		}

		logtt("Trying to create a DataSource for %s from %s file: %s [cwd: %s => %s, file: %s]",
			  root_name.ToString().CStr(),
			  (type == DataType::Xml) ? "XML" : "JSON",
			  _full_file_path.CStr(),
			  current_path.CStr(), _current_file_path.CStr(),
			  file_name.CStr());

		LoadFromFile(_full_file_path, root_name);
	}

	DataSource::DataSource(DataType type, const ov::String &file_path, const ItemName &root_name, cfg::CheckUnknownItems check_unknown_items)
		: DataSource(
			  type,
			  ov::PathManager::ExtractPath(file_path),
			  ov::PathManager::ExtractFileName(file_path),
			  root_name,
			  check_unknown_items)
	{
	}

	void DataSource::LoadFromFile(ov::String file_name, const ItemName &root_name)
	{
		_file_name = file_name;

		logtt("Trying to load data source from %s", file_name.CStr());

		switch (_type)
		{
			case DataType::Xml:
				LoadFromXmlFile(file_name, root_name.GetName(_type));
				return;

			case DataType::Json:
				LoadFromJson(file_name, root_name.GetName(_type));
				return;
		}

		throw CreateConfigError("Not implemented for type: %d (%s)", _type, file_name.CStr());
	}

	void DataSource::LoadFromXmlFile(const ov::String &file_name, const ov::String &root_name)
	{
		auto document				  = std::make_shared<pugi::xml_document>();

		pugi::xml_parse_result result = document->load_file(file_name);

		if (result == false)
		{
			throw CreateConfigError("Could not read the file: %s (reason: %s, offset: %td)",
									file_name.CStr(), result.description(), result.offset);
		}

		_document = document;
		_node	  = document->root().child(root_name);

		if (_node.empty())
		{
			throw CreateConfigError("Could not find the root element: <%s> in %s", root_name.CStr(), file_name.CStr());
		}
	}

	void DataSource::LoadFromJson(const ov::String &file_name, const ov::String &root_name)
	{
		std::ifstream json_file;
		json_file.open(file_name, std::ifstream::in | std::ifstream::binary);

		if (json_file.is_open())
		{
			json_file >> _json;
			json_file.close();
		}
		else
		{
			throw CreateConfigError("Could not read the file: %s", file_name.CStr());
		}
	}

	void DataSource::CheckUnknownItems(const ov::String &path,
									   const std::unordered_map<ov::String, std::shared_ptr<Child>> &children_for_xml,
									   const std::unordered_map<ov::String, std::shared_ptr<Child>> &children_for_json) const
	{
		if (_check_unknown_items == CheckUnknownItems::DontCheck)
		{
			logtt("Checking unknown items is skipped: %s", path.CStr());
			return;
		}

		auto file_path = GetFileName();

		switch (_type)
		{
			case DataType::Xml:
				for (auto &child_node : _node.children())
				{
					ov::String name(child_node.name());

					if (children_for_xml.find(name) == children_for_xml.end())
					{
						if (NeedToIgnore(file_path, child_node, false))
						{
							logtw("Unknown item found, but ignored: %s.%s", path.CStr(), name.CStr());
							continue;
						}

						if (file_path.IsEmpty())
						{
							throw CreateConfigError("Unknown item found: %s.%s", path.CStr(), name.CStr());
						}
						else
						{
							throw CreateConfigError("Unknown item found: %s.%s in %s", path.CStr(), name.CStr(), file_path.CStr());
						}
					}
				}
				break;

			case DataType::Json: {
				if (_json.isObject())
				{
					auto members = _json.getMemberNames();

					for (auto &member : members)
					{
						ov::String name(member.c_str());

						if (name == "$")
						{
							// $ == attributes
							continue;
						}

						if (children_for_json.find(name) == children_for_json.end())
						{
							if (file_path.IsEmpty())
							{
								throw CreateConfigError("Unknown item found: %s.%s", path.CStr(), name.CStr());
							}
							else
							{
								throw CreateConfigError("Unknown item found: %s.%s in %s", path.CStr(), name.CStr(), file_path.CStr());
							}
						}
					}
				}

				break;
			}
		}
	}

	bool DataSource::IsArray(const ItemName &name) const
	{
		switch (_type)
		{
			case DataType::Xml:
				// if (_node)
				// {
				// 	auto iterator = _node.children(name.GetName(DataType::Json));
				// 	auto count = std::distance(iterator.begin(), iterator.end());
				// 	return count > 1;
				// }
				return true;

			case DataType::Json:
				return _json.isArray();
		}

		OV_ASSERT2(false);
		return false;
	}

	Variant DataSource::GetRootValue(ValueType value_type, bool resolve_path, bool omit_json, Json::Value *original_value) const
	{
		switch (_type)
		{
			case DataType::Xml:
				return GetValueFromXml(value_type, "", false, resolve_path, original_value);

			case DataType::Json:
				return GetValueFromJson(value_type, "", false, resolve_path, omit_json, original_value);
		}

		OV_ASSERT2(false);
		return {};
	}

	Variant DataSource::GetValue(ValueType value_type, const ItemName &name, bool resolve_path, bool omit_json, Json::Value *original_value) const
	{
		switch (_type)
		{
			case DataType::Xml:
				return GetValueFromXml(value_type, name.GetName(_type), true, resolve_path, original_value);

			case DataType::Json:
				return GetValueFromJson(value_type, name.GetName(_type), true, resolve_path, omit_json, original_value);
		}

		OV_ASSERT2(false);
		return {};
	}

	Variant DataSource::GetValueFromJson(ValueType value_type, const ov::String &name, bool is_child, bool resolve_path, bool omit_json, Json::Value *original_value) const
	{
		switch (value_type)
		{
			case ValueType::Unknown:
				*original_value = Json::nullValue;
				return {};

			case ValueType::String: {
				auto &json		= is_child ? GetJsonValue(_json, name) : _json;
				*original_value = json;
				return json.isNull() ? Variant() : ov::Converter::ToString(Preprocess(_current_file_path, ov::Converter::ToString(json), resolve_path));
			}

			case ValueType::Integer: {
				auto &json		= is_child ? GetJsonValue(_json, name) : _json;
				*original_value = json;
				return json.isNull() ? Variant() : ov::Converter::ToInt32(json);
			}

			case ValueType::Long: {
				auto &json		= is_child ? GetJsonValue(_json, name) : _json;
				*original_value = json;
				return json.isNull() ? Variant() : ov::Converter::ToInt64(json);
			}

			case ValueType::Boolean: {
				auto &json		= is_child ? GetJsonValue(_json, name) : _json;
				*original_value = json;
				return json.isNull() ? Variant() : ov::Converter::ToBool(json);
			}

			case ValueType::Double: {
				auto &json		= is_child ? GetJsonValue(_json, name) : _json;
				*original_value = json;
				return json.isNull() ? Variant() : ov::Converter::ToDouble(json);
			}

			case ValueType::Attribute: {
				auto attribute	= GetJsonAttribute(_json, name);
				*original_value = attribute;
				return attribute.isNull() ? Variant() : Preprocess(_current_file_path, ov::Converter::ToString(attribute), resolve_path);
			}

			case ValueType::Text: {
				auto &json		= is_child ? GetJsonValue(_json, name) : _json;
				*original_value = json;
				return json.isNull() ? Variant() : Preprocess(_current_file_path, ov::Converter::ToString(json), resolve_path);
			}

			case ValueType::Item: {
				auto &json = is_child ? GetJsonValue(_json, name) : _json;
				SET_ORIGINAL_VALUE_IF_NOT_NULL(json);
				return json.isNull() ? Variant() : DataSource(_current_file_path, _file_name, name, json, _check_unknown_items);
			}

			case ValueType::List: {
				return GetJsonList(_current_file_path, _file_name, _json, name, omit_json, original_value, _check_unknown_items);
			}
		}

		return {};
	}

	bool DataSource::GetIncludeFileList(ov::String *pattern, std::vector<ov::String> *include_file_list) const
	{
		Json::Value dummy_value;
		auto include_file_pattern = GetValue(ValueType::Attribute, "include", false, false, &dummy_value);

		if (include_file_pattern.HasValue())
		{
			auto current_path			 = GetCurrentPath() + "/";

			// Load from the include file
			ov::String include_file_path = include_file_pattern.TryCast<ov::String>();

			logtt("Include file found: %s", include_file_path.CStr());

			std::vector<ov::String> file_list;
			auto path_error = ov::PathManager::GetFileList(current_path, include_file_path, &file_list);

			if (path_error != nullptr)
			{
				throw CreateConfigError("Could not obtain file list: current path: %s, include pattern: %s (%s)", current_path.CStr(), include_file_path.CStr(), path_error->What());
			}

			if (pattern != nullptr)
			{
				*pattern = include_file_path;
			}

			if (include_file_list != nullptr)
			{
				*include_file_list = std::move(file_list);
			}

			return true;
		}

		return false;
	}

	ov::String DataSource::ToString() const
	{
		switch (_type)
		{
			case DataType::Xml: {
				XmlWriter writer;
				_node.print(writer);
				return writer.result;
			}

			case DataType::Json:
				return ov::Json::Stringify(_json, true);
		}

		return "";
	}
}  // namespace cfg
