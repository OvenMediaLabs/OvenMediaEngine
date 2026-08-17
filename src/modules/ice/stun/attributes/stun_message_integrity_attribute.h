//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include "stun_attribute.h"

class StunMessageIntegrityAttribute : public StunAttribute
{
public:
	StunMessageIntegrityAttribute();
	StunMessageIntegrityAttribute(int length);
	virtual ~StunMessageIntegrityAttribute();

	bool Parse(const StunMessage *stun_message, ov::ByteStream &stream) override;

	// 길이가 OV_STUN_HASH_LENGTH인 데이터가 반환됨
	const uint8_t *GetHash() const;
	bool SetHash(const uint8_t hash[OV_STUN_HASH_LENGTH]);

	bool Serialize(const StunMessage *stun_message, ov::ByteStream &stream) const noexcept override;

	ov::String ToString() const override;

protected:
	uint8_t _hash[OV_STUN_HASH_LENGTH];
};
