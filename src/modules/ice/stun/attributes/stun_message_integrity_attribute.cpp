//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================
#include "stun_message_integrity_attribute.h"

#include <base/ovlibrary/ovlibrary.h>

StunMessageIntegrityAttribute::StunMessageIntegrityAttribute()
	: StunMessageIntegrityAttribute(OV_STUN_HASH_LENGTH)
{
}

StunMessageIntegrityAttribute::StunMessageIntegrityAttribute(int length)
	: StunAttribute(StunAttributeType::MessageIntegrity, length)
{
	::memset(_hash, 0, sizeof(_hash));
}

StunMessageIntegrityAttribute::~StunMessageIntegrityAttribute()
{
}

bool StunMessageIntegrityAttribute::Parse(const StunMessage *stun_message, ov::ByteStream &stream)
{
	// 여기서 실제로 무결성 검사 까지는 하지 않음
	if(stream.Read<uint8_t>(_hash, OV_COUNTOF(_hash)) != OV_COUNTOF(_hash))
	{
		return false;
	}

	return true;
}

const uint8_t *StunMessageIntegrityAttribute::GetHash() const
{
	return _hash;
}

bool StunMessageIntegrityAttribute::SetHash(const uint8_t hash[OV_STUN_HASH_LENGTH])
{
	::memcpy(_hash, hash, sizeof(_hash));

	return true;
}

bool StunMessageIntegrityAttribute::Serialize(const StunMessage *stun_message, ov::ByteStream &stream) const noexcept
{
	return StunAttribute::Serialize(stun_message, stream) &&
	       stream.Write<uint8_t>(_hash, OV_STUN_HASH_LENGTH);
}

ov::String StunMessageIntegrityAttribute::ToString() const
{
	return StunAttribute::ToString("StunMessageIntegrityAttribute", ov::String::FormatString(", hash: %s", ov::ToHexString(_hash, OV_COUNTOF(_hash)).CStr()));
}