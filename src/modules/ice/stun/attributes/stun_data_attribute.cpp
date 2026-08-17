//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2021 AirenSoft. All rights reserved.
//
//==============================================================================
#include <base/ovlibrary/ovlibrary.h>
#include "stun_data_attribute.h"

StunDataAttribute::StunDataAttribute()
	: StunDataAttribute(0)
{
}

StunDataAttribute::StunDataAttribute(int length)
	: StunAttribute(StunAttributeType::Data, length)
{
}

StunDataAttribute::~StunDataAttribute()
{
}

bool StunDataAttribute::Parse(const StunMessage *stun_message, ov::ByteStream &stream)
{
	_data = stream.GetRemainData()->Subdata(0, _length);
	stream.Skip(_length);
	return true;
}

const std::shared_ptr<const ov::Data>& StunDataAttribute::GetData() const
{
	return _data;
}

bool StunDataAttribute::SetData(const std::shared_ptr<const ov::Data> &data)
{
	_data = data;
	_length = _data->GetLength();
	return true;
}

bool StunDataAttribute::Serialize(const StunMessage *stun_message, ov::ByteStream &stream) const noexcept
{
	if(_data == nullptr)
	{
		return false;
	}

	return StunAttribute::Serialize(stun_message, stream) &&
	       stream.Write<uint8_t>(_data->GetDataAs<uint8_t>(), _data->GetLength());
}

ov::String StunDataAttribute::ToString() const
{
	return StunAttribute::ToString("StunDataAttribute", ov::String::FormatString(", Data length: %zu", _data->GetLength()));
}