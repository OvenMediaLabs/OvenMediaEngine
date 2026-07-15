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

class StunTextAttributeFormat : public StunAttribute
{
public:
	bool Parse(const StunMessage *stun_message, ov::ByteStream &stream) override
	{
		_text.SetLength(_length);
		return stream.Read<uint8_t>((uint8_t *)(_text.GetBuffer()), _length) == _length;
	}

	const ov::String &GetValue() const
	{
		return _text;
	}

	bool SetText(const ov::String &value)
	{
		_text = value;
		_length = value.GetLength();
		return true;
	}

	bool Serialize(const StunMessage *stun_message, ov::ByteStream &stream) const noexcept override
	{
		return StunAttribute::Serialize(stun_message, stream) && stream.Write<uint8_t>((uint8_t *)(_text.CStr()), _text.GetLength());
	}

	ov::String ToString() const override
	{
		return StunAttribute::ToString(StringFromType(GetType()), 
										ov::String::FormatString(", %s: %s", StringFromType(GetType()),_text.CStr()).CStr());
	}

protected:
	StunTextAttributeFormat(StunAttributeType type, int length)
		: StunAttribute(type, length)
	{
	}

	virtual ~StunTextAttributeFormat()
	{
	}

private:
	ov::String _text;
};
