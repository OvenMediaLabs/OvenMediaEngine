//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2021 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

// auto &GetInt() { return int_value; }
#define CFG_DECLARE_REF_GETTER_OF(getter_name, variable_name) \
	auto &getter_name(bool *is_configured = nullptr)          \
	{                                                         \
		if (is_configured != nullptr)                         \
		{                                                     \
			*is_configured = IsParsed(&variable_name);        \
		}                                                     \
                                                              \
		return variable_name;                                 \
	}

// const auto &GetInt() const { return int_value; }
#define CFG_DECLARE_CONST_REF_GETTER_OF(getter_name, variable_name) \
	const auto &getter_name(bool *is_configured = nullptr) const    \
	{                                                               \
		if (is_configured != nullptr)                               \
		{                                                           \
			*is_configured = IsParsed(&variable_name);              \
		}                                                           \
                                                                    \
		return variable_name;                                       \
	}

// virtual decltype(variable_name) GetInt() { return int_value; }
#define CFG_DECLARE_VIRTUAL_REF_GETTER_OF(getter_name, variable_name)           \
	virtual decltype(variable_name) &getter_name(bool *is_configured = nullptr) \
	{                                                                           \
		if (is_configured != nullptr)                                           \
		{                                                                       \
			*is_configured = IsParsed(&variable_name);                          \
		}                                                                       \
                                                                                \
		return variable_name;                                                   \
	}

// virtual const decltype(variable_name) GetInt() const { return int_value; }
#define CFG_DECLARE_VIRTUAL_CONST_REF_GETTER_OF(getter_name, variable_name)                 \
	virtual const decltype(variable_name) &getter_name(bool *is_configured = nullptr) const \
	{                                                                                       \
		if (is_configured != nullptr)                                                       \
		{                                                                                   \
			*is_configured = IsParsed(&variable_name);                                      \
		}                                                                                   \
                                                                                            \
		return variable_name;                                                               \
	}

// auto GetInt() override { return int_value; }
#define CFG_DECLARE_OVERRIDED_GETTER_OF(getter_name, variable_name) \
	auto getter_name(bool *is_configured = nullptr) override        \
	{                                                               \
		if (is_configured != nullptr)                               \
		{                                                           \
			*is_configured = IsParsed(&variable_name);              \
		}                                                           \
                                                                    \
		return variable_name;                                       \
	}

// auto GetInt() const override { return int_value; }
#define CFG_DECLARE_OVERRIDED_CONST_GETTER_OF(getter_name, variable_name) \
	auto getter_name(bool *is_configured = nullptr) const override        \
	{                                                                     \
		if (is_configured != nullptr)                                     \
		{                                                                 \
			*is_configured = IsParsed(&variable_name);                    \
		}                                                                 \
                                                                          \
		return variable_name;                                             \
	}

#define SET_IF_NOT_PARSED(GETTER, SETTER, VARIABLE) \
	{                                               \
		bool is_configured;                         \
		variant.GETTER(&is_configured);             \
		if (!is_configured)                         \
		{                                           \
			variant.SETTER(VARIABLE);               \
		}                                           \
	}
