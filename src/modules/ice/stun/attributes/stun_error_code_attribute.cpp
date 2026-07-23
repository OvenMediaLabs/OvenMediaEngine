//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2021 AirenSoft. All rights reserved.
//
//==============================================================================

#include "stun_error_code_attribute.h"

StunErrorCodeAttribute::StunErrorCodeAttribute()
	: StunErrorCodeAttribute(0)
{
}

StunErrorCodeAttribute::StunErrorCodeAttribute(int length)
	: StunAttribute(StunAttributeType::ErrorCode, length)
{

}

bool StunErrorCodeAttribute::Parse(const StunMessage *stun_message, ov::ByteStream &stream)
{
	/*
	https://tools.ietf.org/html/rfc8489#section-14.8

	      0                   1                   2                   3
      0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
     +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
     |           Reserved, should be 0         |Class|     Number    |
     +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
     |      Reason Phrase (variable)                                ..
     +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+

                 Figure 7: Format of ERROR-CODE Attribute
	*/

	stream.Skip<uint16_t>(1);
	auto error_class = stream.Read8();
	auto error_number = stream.Read8();

	_code = static_cast<StunErrorCode>(error_class*100 + error_number);
	
	auto reason_length = _length - sizeof(uint32_t);
	_reason.SetLength(reason_length);
	
	return stream.Read<uint8_t>((uint8_t *)(_reason.GetBuffer()), reason_length) == reason_length;
}

// If extra reason is empty, only default error message is used.
bool StunErrorCodeAttribute::SetError(StunErrorCode error_code, ov::String extra_reason)
{
	_code = error_code;

	_reason = StringFromErrorCode(error_code);
	if(extra_reason.IsEmpty() == false)
	{
		_reason.AppendFormat(" - %s", extra_reason.CStr());
	}

	_length = sizeof(uint32_t) + _reason.GetLength();

	return true;
}

uint8_t StunErrorCodeAttribute::GetErrorCodeClass() const
{
	return static_cast<uint16_t>(_code) / 100;
}

uint8_t StunErrorCodeAttribute::GetErrorCodeNumber() const
{
	return static_cast<uint16_t>(_code) % 100;
}

StunErrorCode StunErrorCodeAttribute::GetErrorCode() const
{
	return _code;
}

const ov::String& StunErrorCodeAttribute::GetErrorReason() const
{
	return _reason;
}

bool StunErrorCodeAttribute::Serialize(const StunMessage *stun_message, ov::ByteStream &stream) const noexcept
{
	return StunAttribute::Serialize(stun_message, stream) &&
	       stream.Write16(0x00) &&
	       stream.Write8((uint8_t)GetErrorCodeClass()) &&
	       stream.Write8((uint8_t)GetErrorCodeNumber()) &&
	       stream.Write<uint8_t>((uint8_t *)(_reason.CStr()), _reason.GetLength());
}

ov::String StunErrorCodeAttribute::ToString() const
{
	return StunAttribute::ToString(StringFromType(GetType()), 
								ov::String::FormatString(", %s: (%d) %s", 
											StringFromType(GetType()), static_cast<uint16_t>(_code), _reason.CStr()).CStr());
}