//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2023 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include "stun_attribute.h"

class StunUseCandidateAttribute : public StunAttribute
{
public:
	StunUseCandidateAttribute() : StunAttribute(StunAttributeType::UseCandidate, 0) {}

	bool Parse(const StunMessage *stun_message, ov::ByteStream &stream) override
	{
		return true;
	}

	bool Serialize(const StunMessage *stun_message, ov::ByteStream &stream) const noexcept override
	{
		return StunAttribute::Serialize(stun_message, stream);
	}

	ov::String ToString() const override
	{
		return StunAttribute::ToString(StringFromType(GetType()), "");
	}
};

