//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2021 AirenSoft. All rights reserved.
//
//==============================================================================
#include "stun_requested_transport_attribute.h"
#include <base/ovlibrary/ovlibrary.h>

StunRequestedTransportAttribute::StunRequestedTransportAttribute()
	:StunRequestedTransportAttribute(REQUESTED_TRANSPORT_ATTRIBUTE_VALUE_LENGTH)
{
}

StunRequestedTransportAttribute::StunRequestedTransportAttribute(int length)
	: StunAttribute(StunAttributeType::RequestedTransport, length)
{
}

StunRequestedTransportAttribute::~StunRequestedTransportAttribute()
{
}

bool StunRequestedTransportAttribute::Parse(const StunMessage *stun_message, ov::ByteStream &stream)
{
	/*
	 0                   1                   2                   3
	 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
	+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	|    Protocol   |                    RFFU                       |
	+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	*/
	// 4 bytes	
	_protocol_number = stream.Read8();
	
	// The RFFU field MUST be set to zero on transmission and MUST be ignored on reception. 
	// It is reserved for future uses.
	auto rffu = stream.ReadBE24();
	if(rffu != 0)
	{
		return false;
	}

	return true;
}

uint8_t StunRequestedTransportAttribute::GetProtocolNumber() const
{
	return _protocol_number;
}

bool StunRequestedTransportAttribute::SetProtocolNumber(const uint8_t number)
{
	_protocol_number = number;
	return true;	
}

bool StunRequestedTransportAttribute::Serialize(const StunMessage *stun_message, ov::ByteStream &stream) const noexcept
{
	return StunAttribute::Serialize(stun_message, stream) && stream.Write8(_protocol_number) && stream.Write24(0);
}

ov::String StunRequestedTransportAttribute::ToString() const
{
	return StunAttribute::ToString("StunRequestedTransportAttribute", 
									ov::String::FormatString(", Protocol : %d", _protocol_number));
}