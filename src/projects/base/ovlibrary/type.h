//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2021 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <type_traits>

namespace ov
{
	template <typename T>
	constexpr inline std::underlying_type_t<T> ToUnderlyingType(T enum_value)
	{
		return static_cast<std::underlying_type_t<T>>(enum_value);
	}

	template <typename T>
	using UnderylingType = std::underlying_type_t<T>;
}  // namespace ov

#define OV_DEFINE_GETTER(getter_name, variable, ...) \
	auto getter_name() __VA_ARGS__                   \
	{                                                \
		return variable;                             \
	}

#define OV_DEFINE_CONST_GETTER(getter_name, variable, ...) \
	const auto &getter_name() const __VA_ARGS__            \
	{                                                      \
		return variable;                                   \
	}
