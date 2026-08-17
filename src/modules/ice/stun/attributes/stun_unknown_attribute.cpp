//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================
#include "stun_unknown_attribute.h"

StunUnknownAttribute::StunUnknownAttribute(int type, int length)
	: StunAttribute(StunAttributeType::UnknownAttributes, type, length)
{
}

StunUnknownAttribute::~StunUnknownAttribute()
{
}

bool StunUnknownAttribute::Parse(const StunMessage *stun_message, ov::ByteStream &stream)
{
	// unknown attribute는 그냥 skip
	return stream.Skip<uint8_t>(_length) == _length;
}

bool StunUnknownAttribute::SetData(const void *data, int length)
{
	_data = std::make_shared<ov::Data>(data, length);
	_length = length;

	return true;
}

bool StunUnknownAttribute::Serialize(const StunMessage *stun_message, ov::ByteStream &stream) const noexcept
{
	return StunAttribute::Serialize(stun_message, stream) && ((_data != nullptr) ? stream.Write(_data) : true);
}

ov::String StunUnknownAttribute::ToString() const
{
	return StunAttribute::ToString("StunUnknownAttribute", "");
}
