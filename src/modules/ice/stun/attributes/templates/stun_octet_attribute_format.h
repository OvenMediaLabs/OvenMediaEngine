//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2021 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include "modules/ice/stun/attributes/stun_attribute.h"
#include "modules/ice/stun/stun_datastructure.h"

template<typename T>
class StunOctetAttributeFormat : public StunAttribute
{
public:
	virtual bool Parse(const StunMessage *stun_message, ov::ByteStream &stream) override
	{
		if(stream.IsRemained(sizeof(T)) == false)
		{
			return false;
		}

		stream.ReadBE(_value);

		return true;
	}

	virtual T GetValue() const
	{
		return _value;
	}

	virtual bool SetValue(T value)
	{
		_value = value;
		return true;
	}

	bool Serialize(const StunMessage *stun_message, ov::ByteStream &stream) const noexcept override
	{
		return StunAttribute::Serialize(stun_message, stream) && stream.WriteBE(static_cast<T>(_value));
	}

	ov::String ToString() const override
	{
		using U = std::make_unsigned_t<T>;
		const char *format_string = nullptr;
		U value;

		if constexpr (sizeof(U) == 4)
		{
			format_string = ", value : %08" PRIX32;
			value = static_cast<std::uint32_t>(static_cast<U>(_value));
		}
		else if constexpr (sizeof(U) == 8)
		{
			format_string = ", value : %016" PRIX64;
			value = static_cast<std::uint64_t>(static_cast<U>(_value));
		}
		else
		{
			format_string = ", value : %" PRIXMAX;
			value = static_cast<std::uintmax_t>(static_cast<U>(_value));
		}

		return StunAttribute::ToString(StringFromType(GetType()), ov::String::FormatString(format_string, value).CStr());
	}

protected:
	StunOctetAttributeFormat(StunAttributeType type, int length) : StunAttribute(type, length) {}

	T _value;
};