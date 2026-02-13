//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2021 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <any>
#include <functional>

#include "./annotations.h"
#include "./attribute.h"
#include "./child.h"
#include "./config_error.h"
#include "./data_source.h"
#include "./declare_utilities.h"
#include "./item_name.h"
#include "./list.h"

#define CFG_VERBOSE_STRING 1
#define CFG_PRINT_MEMORY_ADDRESS 0

#if CFG_VERBOSE_STRING
#	define CFG_EXTRA_PREFIX "                // <"
#	define CFG_EXTRA_SUFFIX ">"
#else  // CFG_VERBOSE_STRING
#	define CFG_EXTRA_PREFIX ""
#	define CFG_EXTRA_SUFFIX ""
#endif	// CFG_VERBOSE_STRING

namespace cfg
{
	class Item
	{
	protected:
		static ov::String ChildToString(int indent_count, const std::shared_ptr<const Child> &child, size_t index, size_t child_count);

		friend class SetValueHelper;

	public:
		Item() = default;

		Item(const Item &item);
		Item(Item &&item);

		virtual ~Item() = default;

		MAY_THROWS(cfg::ConfigError)
		void FromDataSource(ov::String item_path, const ItemName &name, const DataSource &data_source, bool allow_optional = false);

		MAY_THROWS(cfg::ConfigError)
		void FromDataSource(const DataSource &data_source, bool allow_optional = false);

		MAY_THROWS(cfg::ConfigError)
		void FromJson(const Json::Value &value, bool allow_optional = false);

		Item &operator=(const Item &item);

		const ItemName &GetItemName() const
		{
			return _item_name;
		}

		void SetItemName(const ItemName &item_name)
		{
			_item_name = item_name;
		}

		bool IsParsed() const
		{
			return _is_parsed;
		}

		void SetParsed(bool is_parsed)
		{
			_is_parsed = is_parsed;
		}

		bool IsReadOnly() const
		{
			return _is_read_only;
		}

		void SetReadOnly(bool is_read_only)
		{
			_is_read_only = is_read_only;
		}

		template <typename Ttype = Child>
		std::shared_ptr<Ttype> GetMember(const void *member_pointer)
		{
			RebuildListIfNeeded();

			for (const auto &child : _children)
			{
				if (child->GetMemberRawPointer() == member_pointer)
				{
					return child->GetSharedPtrAs<Ttype>();
				}
			}

			return nullptr;
		}

		template <typename Ttype>
		std::shared_ptr<List<Ttype>> GetListMember(const std::vector<Ttype> *member_pointer)
		{
			return GetMember<List<Ttype>>(member_pointer);
		}

		void SetValue(const void *member_pointer, const Variant &value, bool is_parent_optional)
		{
			RebuildListIfNeeded();

			for (const auto &child : _children)
			{
				if (child->GetMemberRawPointer() == member_pointer)
				{
					child->SetValue(value, is_parent_optional);
					return;
				}
			}
		}

		MAY_THROWS(cfg::ConfigError)
		void ValidateOmitJsonNameRules(const ov::String &item_path) const;

		MAY_THROWS(cfg::ConfigError)
		void ValidateOmitJsonNameRules() const;

		virtual ov::String ToString() const
		{
			return ToString(0);
		}

		virtual ov::String ToString(int indent_count) const;

		// Add `original_value` to `object[child_name]` based on `value_type`.
		static void SetOriginalJsonValue(ValueType value_type, Json::Value &object, const ov::String &child_name, const Json::Value &original_value);
		// Add a value retrieved from `member` according to the `value_type` to `object[child_name]`.
		static void SetCurrentJsonValue(ValueType value_type, Json::Value &object, const ov::String &child_name, const Variant &member);

		/// Get the configurations in `Json::Value` format.
		///
		/// @param value The `Json::Value` object to store the configuration.
		/// @param original_value If `true`, it returns the raw, unprocessed value from the settings.
		///                       If `false`, it returns the value with `ResolvePath`, `${env}`, and similar elements processed.
		/// @param include_default_values If `true`, it includes the default values in the output.
		MAY_THROWS(cfg::ConfigError)
		void ToJson(Json::Value &value, bool original_value, bool include_default_values) const;
		MAY_THROWS(cfg::ConfigError)
		Json::Value ToJson(bool original_value = true, bool include_default_values = false) const
		{
			Json::Value value;
			ToJson(value, original_value, include_default_values);
			return value;
		}

		/// Get the configurations in `pugi::xml_node`/`pugi::xml_document` format.
		///
		/// @param value The `Json::Value` object to store the raw configuration.
		/// @param include_default_values If `true`, it includes the default values in the output.
		MAY_THROWS(cfg::ConfigError)
		void ToXml(pugi::xml_node &node, bool include_default_values) const;
		MAY_THROWS(cfg::ConfigError)
		pugi::xml_document ToXml(bool include_default_values = false) const
		{
			pugi::xml_document doc;
			auto root_node = doc.append_child();
			ToXml(root_node, include_default_values);
			return doc;
		}

	protected:
		MAY_THROWS(cfg::ConfigError)
		void FromDataSourceInternal(ov::String item_path, const DataSource &data_source, bool is_parent_optional);

		MAY_THROWS(cfg::ConfigError)
		bool IsParsed(const void *target) const;

		MAY_THROWS(cfg::ConfigError)
		void ValidateOmitJsonNameRuleForItem(const ov::String &item_path, const std::shared_ptr<Child> &child) const;

		MAY_THROWS(cfg::ConfigError)
		void ValidateOmitJsonNameRuleForList(const ov::String &item_path, const std::shared_ptr<Child> &child) const;

		// When converting to JSON, check if the sub-items of this item can be omitted.
		// If an error is thrown when call this function, there is a misdeveloped config.
		//
		// For example, the following cases may be omitted:
		//
		// <Items>
		//     <Item>Hello</Item>
		//     <Item>World</Item>
		// </Items>
		// =>
		// { "items": [ "Hello", "World" ] }
		//
		// But, the following cases cannot be omitted:
		//
		// <Items>
		//     <Item>Hello</Item>
		//     <Item>World</Item>
		//     <AnotherItem />
		// </Items>

		// @param item_path The path of this item (for debugging purpose)
		// @param name Item name
		MAY_THROWS(cfg::ConfigError)
		void ValidateOmitJsonNameRule(const ov::String &item_path, const ov::String &name) const;

		void RebuildListIfNeeded() const;
		void RebuildListIfNeeded();

		virtual void MakeList() = 0;

		void AddChild(const ItemName &name, ValueType type, const ov::String &type_name,
					  Optional is_optional, cfg::ResolvePath resolve_path, cfg::OmitJsonName omit_json_name,
					  OptionalCallback optional_callback, ValidationCallback validation_callback,
					  void *member_raw_pointer, std::any member_pointer);

		void AddChild(const std::shared_ptr<Child> &child);

		// For primitive types
		template <
			typename Tannot1 = void, typename Tannot2 = void, typename Tannot3 = void,
			typename Ttype, std::enable_if_t<!std::is_base_of_v<Item, Ttype> && !std::is_base_of_v<Text, Ttype>, int> = 0>
		void Register(const ItemName &name, Ttype *value, OptionalCallback optional_callback = nullptr, ValidationCallback validation_callback = nullptr)
		{
			AddChild(name, ProbeType<Ttype>::type, ov::Demangle(typeid(Ttype).name()),
					 CheckAnnotations<Optional, Tannot1, Tannot2, Tannot3>::value ? Optional::Optional : Optional::NotOptional,
					 CheckAnnotations<ResolvePath, Tannot1, Tannot2, Tannot3>::value ? ResolvePath::Resolve : ResolvePath::DontResolve,
					 CheckAnnotations<OmitJsonName, Tannot1, Tannot2, Tannot3>::value ? OmitJsonName::Omit : OmitJsonName::DontOmit,
					 optional_callback, validation_callback,
					 value, value);
		}

		// For Text
		template <
			typename Tannot1 = void, typename Tannot2 = void, typename Tannot3 = void,
			typename Ttype, std::enable_if_t<std::is_base_of_v<Text, Ttype>, int> = 0>
		void Register(const ItemName &name, Ttype *value, OptionalCallback optional_callback = nullptr, ValidationCallback validation_callback = nullptr)
		{
			AddChild(name, ProbeType<Ttype>::type, ov::Demangle(typeid(Ttype).name()),
					 CheckAnnotations<Optional, Tannot1, Tannot2, Tannot3>::value ? Optional::Optional : Optional::NotOptional,
					 CheckAnnotations<ResolvePath, Tannot1, Tannot2, Tannot3>::value ? ResolvePath::Resolve : ResolvePath::DontResolve,
					 CheckAnnotations<OmitJsonName, Tannot1, Tannot2, Tannot3>::value ? OmitJsonName::Omit : OmitJsonName::DontOmit,
					 optional_callback, validation_callback,
					 value, static_cast<Text *>(value));
		}

		// For Item
		template <
			typename Tannot1 = void, typename Tannot2 = void, typename Tannot3 = void,
			typename Ttype, std::enable_if_t<std::is_base_of_v<Item, Ttype>, int> = 0>
		void Register(const ItemName &name, Ttype *value, OptionalCallback optional_callback = nullptr, ValidationCallback validation_callback = nullptr)
		{
			AddChild(name, ProbeType<Ttype>::type, ov::Demangle(typeid(Ttype).name()),
					 CheckAnnotations<Optional, Tannot1, Tannot2, Tannot3>::value ? Optional::Optional : Optional::NotOptional,
					 CheckAnnotations<ResolvePath, Tannot1, Tannot2, Tannot3>::value ? ResolvePath::Resolve : ResolvePath::DontResolve,
					 CheckAnnotations<OmitJsonName, Tannot1, Tannot2, Tannot3>::value ? OmitJsonName::Omit : OmitJsonName::DontOmit,
					 optional_callback, validation_callback,
					 value, static_cast<Item *>(value));

			value->SetItemName(name);
		}

		// For std::vector<Ttype>
		template <
			typename Tannot1 = void, typename Tannot2 = void, typename Tannot3 = void,
			typename Ttype>
		void Register(const ItemName &name, std::vector<Ttype> *value, OptionalCallback optional_callback = nullptr, ValidationCallback validation_callback = nullptr)
		{
			AddChild(
				std::make_shared<List<Ttype>>(
					name, ov::Demangle(typeid(Ttype).name()),
					CheckAnnotations<Optional, Tannot1, Tannot2, Tannot3>::value ? Optional::Optional : Optional::NotOptional,
					CheckAnnotations<ResolvePath, Tannot1, Tannot2, Tannot3>::value ? ResolvePath::Resolve : ResolvePath::DontResolve,
					CheckAnnotations<OmitJsonName, Tannot1, Tannot2, Tannot3>::value ? OmitJsonName::Omit : OmitJsonName::DontOmit,
					optional_callback, validation_callback,
					value));
		}

		// Used to determine if a child list needs to be refreshed
		//
		// _last_target == this: The list is not refreshed
		// _last_target != this: The list is refreshed
		const void *_last_target = nullptr;

		bool _is_parsed			 = false;
		bool _is_read_only		 = true;

		ItemName _item_name;

		std::vector<std::shared_ptr<Child>> _children;
		std::unordered_map<ov::String, std::shared_ptr<Child>> _children_for_xml;
		std::unordered_map<ov::String, std::shared_ptr<Child>> _children_for_json;
	};
}  // namespace cfg
