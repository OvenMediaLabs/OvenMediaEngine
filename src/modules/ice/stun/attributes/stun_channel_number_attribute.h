//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2021 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include "stun_attribute.h"
#include "modules/ice/stun/stun_datastructure.h"

class StunChannelNumberAttribute : public StunAttribute
{
public:
	StunChannelNumberAttribute():StunChannelNumberAttribute(0){}
	StunChannelNumberAttribute(int length):StunAttribute(StunAttributeType::ChannelNumber, length){}

	bool Parse(const StunMessage *stun_message, ov::ByteStream &stream) override
	{
		if(stream.IsRemained(sizeof(uint32_t)) == false)
		{
			return false;
		}

		_channel_number = stream.ReadBE16();
		auto rffu = stream.ReadBE16();
		// Must be 0
		if(rffu != 0)
		{
			return false;
		}

		return true;
	}

	uint16_t GetChannelNumber() const
	{
		return _channel_number;
	}

	bool SetChannelNumber(uint16_t number)
	{
		_channel_number = number;
		return true;
	}

	bool Serialize(const StunMessage *stun_message, ov::ByteStream &stream) const noexcept override
	{
		return StunAttribute::Serialize(stun_message, stream) && stream.WriteBE16(_channel_number) && stream.WriteBE16(0);
	}

	ov::String ToString() const override
	{
		return StunAttribute::ToString(StringFromType(GetType()), ov::String::FormatString(", channel number : %08X", _channel_number).CStr());
	}

private:
	uint16_t	_channel_number;
};