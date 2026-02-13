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

#include <functional>

#include "./annotations.h"
#include "./config_error.h"
#include "./data_source.h"
#include "./item_name.h"
#include "./value_type.h"

namespace cfg
{
	class Item;

	// A callback called to determine if the value is conditional optional
	using OptionalCallback	 = std::function<std::shared_ptr<ConfigError>()>;

	using ValidationCallback = std::function<std::shared_ptr<ConfigError>()>;

	class Child : public ov::EnableSharedFromThis<Child>
	{
		friend class Item;

	public:
		Child() = default;
		Child(const ItemName &item_name, ValueType type, const ov::String &type_name,
			  Optional is_optional, ResolvePath resolve_path, OmitJsonName omit_json_name,
			  OptionalCallback optional_callback, ValidationCallback validation_callback,
			  void *member_raw_pointer, Variant member_pointer)
			: _item_name(item_name),
			  _type(type),
			  _type_name(type_name),

			  _is_optional(is_optional),
			  _resolve_path(resolve_path),
			  _omit_json_name(omit_json_name),

			  _optional_callback(optional_callback),
			  _validation_callback(validation_callback),

			  _member_raw_pointer(member_raw_pointer),
			  _member_pointer(member_pointer)
		{
		}

		virtual ~Child() = default;

		void CopyFrom(const std::shared_ptr<const Child> &another_child)
		{
			_is_parsed		= another_child->_is_parsed;

			_original_value = another_child->_original_value;
		}

		void SetParsed(bool is_parsed)
		{
			_is_parsed = is_parsed;
		}

		bool IsParsed() const
		{
			return _is_parsed;
		}

		void SetItemName(const ItemName &item_name)
		{
			_item_name = item_name;
		}

		const ItemName &GetItemName() const
		{
			return _item_name;
		}

		ValueType GetType() const
		{
			return _type;
		}

		const ov::String &GetTypeName() const
		{
			return _type_name;
		}

		bool IsOptional() const
		{
			return _is_optional == Optional::Optional;
		}

		bool ResolvePath() const
		{
			return _resolve_path == ResolvePath::Resolve;
		}

		bool OmitJsonName() const
		{
			return _omit_json_name == OmitJsonName::Omit;
		}

		OptionalCallback GetOptionalCallback() const
		{
			return _optional_callback;
		}

		ValidationCallback GetValidationCallback() const
		{
			return _validation_callback;
		}

		std::shared_ptr<ConfigError> CallOptionalCallback() const
		{
			return (_optional_callback != nullptr) ? _optional_callback() : nullptr;
		}

		std::shared_ptr<ConfigError> CallValidationCallback() const
		{
			return (_validation_callback != nullptr) ? _validation_callback() : nullptr;
		}

		Variant &GetMemberPointer()
		{
			return _member_pointer;
		}

		const Variant &GetMemberPointer() const
		{
			return _member_pointer;
		}

		MAY_THROWS(cfg::CastException)
		template <typename Toutput_type, typename... Ttype>
		Toutput_type *GetMemberPointerAs()
		{
			return _member_pointer.TryCast<Toutput_type *, Ttype...>();
		}

		MAY_THROWS(cfg::CastException)
		template <typename Toutput_type, typename... Ttype>
		const Toutput_type *GetMemberPointerAs() const
		{
			return _member_pointer.TryCast<Toutput_type *, Ttype...>();
		}

		void *GetMemberRawPointer()
		{
			return _member_raw_pointer;
		}

		const void *GetMemberRawPointer() const
		{
			return _member_raw_pointer;
		}

		const Json::Value &GetOriginalValue() const
		{
			return _original_value;
		}

		MAY_THROWS(cfg::ConfigError)
		void SetValue(const Variant &value, bool is_parent_optional);

		MAY_THROWS(cfg::ConfigError)
		void SetValue(const ov::String &item_path, const DataSource &data_source, bool is_parent_optional);

	protected:
		void SetOriginalValue(Json::Value value)
		{
			_original_value = std::move(value);
		}

		bool _is_parsed = false;

		ItemName _item_name;
		ValueType _type = ValueType::Unknown;
		ov::String _type_name;

		cfg::Optional _is_optional				= Optional::NotOptional;
		cfg::ResolvePath _resolve_path			= ResolvePath::DontResolve;
		cfg::OmitJsonName _omit_json_name		= OmitJsonName::DontOmit;

		OptionalCallback _optional_callback		= nullptr;
		ValidationCallback _validation_callback = nullptr;

		// This value contains a pointer to point a member variable
		void *_member_raw_pointer;
		// This value contains the pointer + a type of the member variable
		Variant _member_pointer;

		Json::Value _original_value;
	};
}  // namespace cfg
