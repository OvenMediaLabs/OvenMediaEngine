//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2022 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <any>

#include "./config_error.h"
#include "./value_type.h"

namespace cfg
{
	class Variant : protected std::any
	{
	public:
		Variant()
		{
#if DEBUG
			_type_name = GetTypeName();
#endif	// DEBUG
		}

		template <typename Ttype>
		Variant(const Ttype &value)
			: std::any(value)
		{
#if DEBUG
			_type_name = GetTypeName();
#endif	// DEBUG
		}

		bool HasValue() const
		{
			return has_value();
		}

		ov::String GetTypeName() const
		{
			return ov::Demangle(type().name());
		}

		MAY_THROWS(cfg::CastException)
		MAY_THROWS(cfg::ConfigError)
		void SetValue(ValueType value_type, const Variant &value, Json::Value *original_value = nullptr);

		MAY_THROWS(cfg::CastException)
		template <typename Toutput_type>
		Toutput_type &TryCast()
		{
			try
			{
				return std::any_cast<Toutput_type &>(*this);
			}
			catch (const std::bad_any_cast &)
			{
				throw CastException(
					GetTypeName(),
					ov::Demangle(typeid(Toutput_type).name()).CStr());
			}
		}

		MAY_THROWS(cfg::CastException)
		template <typename Toutput_type>
		const Toutput_type TryCast() const
		{
			try
			{
				return std::any_cast<const Toutput_type>(*this);
			}
			catch (const std::bad_any_cast &)
			{
				throw CastException(
					GetTypeName(),
					ov::Demangle(typeid(Toutput_type).name()).CStr());
			}
		}

		MAY_THROWS(cfg::CastException)
		template <typename Toutput_type, typename Ttype, typename... Tcandidates>
		Toutput_type &TryCast()
		{
			try
			{
				return std::any_cast<Ttype &>(*this);
			}
			catch (const std::bad_any_cast &)
			{
			}

			return TryCast<Toutput_type, Tcandidates...>();
		}

		MAY_THROWS(cfg::CastException)
		template <typename Toutput_type, typename Ttype, typename... Tcandidates>
		const Toutput_type TryCast() const
		{
			try
			{
				return std::any_cast<const Ttype>(*this);
			}
			catch (const std::bad_any_cast &)
			{
			}

			return TryCast<Toutput_type, Tcandidates...>();
		}

#if DEBUG
		ov::String _type_name;
#endif	// DEBUG
	};
}  // namespace cfg
