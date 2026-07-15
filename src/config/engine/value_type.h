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

#include <any>
#include <string>
#include <vector>

#include "config_error.h"

namespace cfg
{
	class Attribute;
	class Text;
	class Item;
	class ListInterface;

	//--------------------------------------------------------------------
	// ValueType
	//--------------------------------------------------------------------
	enum class ValueType : uint32_t
	{
		//
		// Primitive types
		//
		// Unknown type
		Unknown,
		// A text value
		String,
		// A 32-bits integer value
		Integer,
		// A 64-bits integer value
		Long,
		// A boolean value
		Boolean,
		// A double value
		Double,
		// An attribute
		Attribute,
		// A text of itself
		Text,

		//
		// Non-primitive types
		//
		// An item
		Item,
		// A list value
		List
	};

	const char *StringFromValueType(ValueType type);
	//--------------------------------------------------------------------

	enum class CheckUnknownItems
	{
		Check,
		DontCheck
	};

	//--------------------------------------------------------------------
	// ProbeType
	//--------------------------------------------------------------------
	// Probe a type of Tprobe
	template <typename Tprobe, typename Tdummy = void>
	struct ProbeType
	{
		static constexpr ValueType type		= ValueType::Unknown;
		static constexpr ValueType sub_type = ValueType::Unknown;
	};

	template <>
	struct ProbeType<ov::String>
	{
		static constexpr ValueType type		= ValueType::String;
		static constexpr ValueType sub_type = ValueType::Unknown;
	};

	template <>
	struct ProbeType<int32_t>
	{
		static constexpr ValueType type		= ValueType::Integer;
		static constexpr ValueType sub_type = ValueType::Unknown;
	};

	template <>
	struct ProbeType<int64_t>
	{
		static constexpr ValueType type		= ValueType::Long;
		static constexpr ValueType sub_type = ValueType::Unknown;
	};

	template <>
	struct ProbeType<bool>
	{
		static constexpr ValueType type		= ValueType::Boolean;
		static constexpr ValueType sub_type = ValueType::Unknown;
	};

	template <>
	struct ProbeType<double>
	{
		static constexpr ValueType type		= ValueType::Double;
		static constexpr ValueType sub_type = ValueType::Unknown;
	};

	template <>
	struct ProbeType<Attribute>
	{
		static constexpr ValueType type		= ValueType::Attribute;
		static constexpr ValueType sub_type = ValueType::Unknown;
	};

	template <typename Tprobe>
	struct ProbeType<Tprobe, std::enable_if_t<std::is_base_of_v<Text, Tprobe>>>
	{
		static constexpr ValueType type		= ValueType::Text;
		static constexpr ValueType sub_type = ValueType::Unknown;
	};

	// Tprobe instance of Item
	template <typename Tprobe>
	struct ProbeType<Tprobe, std::enable_if_t<std::is_base_of_v<Item, Tprobe>>>
	{
		static constexpr ValueType type		= ValueType::Item;
		static constexpr ValueType sub_type = ValueType::Unknown;
	};

	template <typename Titem>
	struct ProbeType<std::vector<Titem>>
	{
		static constexpr ValueType type		= ValueType::List;
		static constexpr ValueType sub_type = ProbeType<Titem>::type;
	};
	//--------------------------------------------------------------------

	//--------------------------------------------------------------------
	// MakeAny()
	//--------------------------------------------------------------------
	// Convert <Subclass of cfg::Text *> to std::any
	template <typename Ttype, std::enable_if_t<std::is_base_of_v<Text, Ttype>, int> = 0>
	static std::any MakeAny(Ttype *item)
	{
		return static_cast<Text *>(item);
	}

	// Convert <Subclass of cfg::Item *> to std::any
	template <typename Ttype, std::enable_if_t<std::is_base_of_v<Item, Ttype>, int> = 0>
	static std::any MakeAny(Ttype *item)
	{
		return static_cast<Item *>(item);
	}

	// Convert other types to std::any
	template <typename Ttype,
			  std::enable_if_t<!std::is_base_of_v<Text, Ttype>, int> = 0,
			  std::enable_if_t<!std::is_base_of_v<Item, Ttype>, int> = 0>
	static std::any MakeAny(Ttype *item)
	{
		return item;
	}
	//--------------------------------------------------------------------

	ov::String MakeIndentString(int indent_count);

	//--------------------------------------------------------------------
	// ToDebugString()
	//--------------------------------------------------------------------
	ov::String ToDebugString(int indent_count, const ov::String *value);
	ov::String ToDebugString(int indent_count, const int *value);
	ov::String ToDebugString(int indent_count, const int64_t *value);
	ov::String ToDebugString(int indent_count, const bool *value);
	ov::String ToDebugString(int indent_count, const double *value);
	ov::String ToDebugString(int indent_count, const Attribute *value);
	ov::String ToDebugString(int indent_count, const Text *value);
	ov::String ToDebugString(int indent_count, const Item *value);
	//--------------------------------------------------------------------

	//--------------------------------------------------------------------
	// ToString()
	//--------------------------------------------------------------------
	template <typename Tvalue_type, std::enable_if_t<!std::is_base_of_v<Item, Tvalue_type> && !std::is_base_of_v<Text, Tvalue_type>, int> = 0>
	ov::String ToString(const Tvalue_type *value)
	{
		return ov::Converter::ToString(*value);
	}
	ov::String ToString(const Text *value);
	ov::String ToString(int indent_count, const Item *value);
	//--------------------------------------------------------------------

	struct CastException
	{
		CastException(ov::String from, ov::String to)
			: from(from),
			  to(to)
		{
		}

		ov::String from;
		ov::String to;
	};
}  // namespace cfg
