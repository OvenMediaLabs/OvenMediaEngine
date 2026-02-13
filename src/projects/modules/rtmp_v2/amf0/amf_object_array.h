//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2025 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <base/ovlibrary/ovlibrary.h>
#include <string.h>

#include <vector>

#include "./amf_property_pair.h"

namespace modules::rtmp
{
	// Key/Value based object
	class AmfObjectArray : AmfPropertyBase
	{
	public:
		AmfObjectArray(AmfTypeMarker type);
		// copy ctor
		AmfObjectArray(const AmfObjectArray &other);
		// move ctor
		AmfObjectArray(AmfObjectArray &&other) noexcept;

		AmfObjectArray &operator=(const AmfObjectArray &other);
		AmfObjectArray &operator=(AmfObjectArray &&other) noexcept;

		bool Append(const char *name, const AmfProperty &property);

		bool Encode(ov::ByteStream &byte_stream, bool encode_marker) const override;
		bool Decode(ov::ByteStream &byte_stream, bool decode_marker) override;

		const AmfPropertyPair *GetPair(const char *name) const;
		const AmfPropertyPair *GetPair(const char *name, AmfTypeMarker expected_type) const;

		// Get the value of the property corresponding to the name.
		// If the property does not exist or its type is not boolean, it returns `std::nullopt`.
		std::optional<bool> GetBoolean(const char *name) const;
		template <typename... Tkey>
		std::optional<bool> GetBoolean(Tkey &&...keys) const
		{
			return GetValue<double>(&AmfObjectArray::GetBoolean, std::forward<Tkey>(keys)...);
		}

		// Get the value of the property corresponding to the name.
		// If the property does not exist or its type is not number, it returns `std::nullopt`.
		std::optional<double> GetNumber(const char *name) const;
		template <typename T>
		std::optional<T> GetNumberAs(const char *name) const
		{
			auto number = GetNumber(name);
			if (number.has_value())
			{
				return static_cast<T>(number.value());
			}
			return std::nullopt;
		}

		template <typename... Tkey>
		std::optional<double> GetNumber(Tkey &&...keys) const
		{
			return GetValue<double>(&AmfObjectArray::GetNumber, std::forward<Tkey>(keys)...);
		}
		template <typename T, typename... Tkey>
		std::optional<T> GetNumberAs(Tkey &&...keys) const
		{
			auto number = GetValue<double>(&AmfObjectArray::GetNumber, std::forward<Tkey>(keys)...);
			if (number.has_value())
			{
				return static_cast<T>(number.value());
			}
			return std::nullopt;
		}

		// Get the value of the property corresponding to the name.
		// If the property exists and its type is not number, it converts it to a double,
		// otherwise it returns `std::nullopt`.
		std::optional<double> GetAsNumber(const char *name) const;
		template <typename T>
		std::optional<T> GetAsNumberAs(const char *name) const
		{
			auto number = GetAsNumber(name);
			if (number.has_value())
			{
				return static_cast<T>(number.value());
			}
			return std::nullopt;
		}
		template <typename... Tkey>
		std::optional<double> GetAsNumber(Tkey &&...keys) const
		{
			return GetValue<double>(&AmfObjectArray::GetAsNumber, std::forward<Tkey>(keys)...);
		}
		template <typename T, typename... Tkey>
		std::optional<T> GetAsNumberAs(Tkey &&...keys) const
		{
			auto number = GetValue<double>(&AmfObjectArray::GetAsNumber, std::forward<Tkey>(keys)...);
			if (number.has_value())
			{
				return static_cast<T>(number.value());
			}
			return std::nullopt;
		}

		std::optional<ov::String> GetString(const char *name) const;
		template <typename... Tkey>
		std::optional<ov::String> GetString(Tkey &&...keys) const
		{
			return GetValue<ov::String>(&AmfObjectArray::GetString, std::forward<Tkey>(keys)...);
		}

		void ToString(ov::String &description, size_t indent = 0) const override;
		ov::String ToString(size_t indent = 0) const override;

	protected:
		template <typename Treturn, typename... Tkey>
		std::optional<Treturn> GetValue(
			std::optional<Treturn> (AmfObjectArray::*getter)(const char *) const,
			Tkey &&...keys) const
		{
			std::optional<Treturn> result;
			(..., (result = result ? result : (this->*getter)(std::forward<Tkey>(keys))));
			return result;
		}

		std::vector<AmfPropertyPair> _amf_property_pairs;
	};

	class AmfObject : public AmfObjectArray
	{
	public:
		AmfObject() : AmfObjectArray(AmfTypeMarker::Object) {}
	};

	class AmfEcmaArray : public AmfObjectArray
	{
	public:
		AmfEcmaArray() : AmfObjectArray(AmfTypeMarker::EcmaArray) {}
	};

	template <typename T>
	class AmfBuilder
	{
	public:
		AmfBuilder<T> &Append(const char *name, const AmfProperty &property)
		{
			_instance.Append(name, property);
			return *this;
		}

		T Build()
		{
			return std::move(_instance);
		}

	private:
		T _instance;
	};

	using AmfObjectBuilder	  = AmfBuilder<AmfObject>;
	using AmfEcmaArrayBuilder = AmfBuilder<AmfEcmaArray>;
}  // namespace modules::rtmp
