//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2021 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include "templates/stun_text_attribute_format.h"

class StunNonceAttribute : public StunTextAttributeFormat
{
public:
	StunNonceAttribute() : StunNonceAttribute(0){}
	StunNonceAttribute(int length) : StunTextAttributeFormat(StunAttributeType::Nonce, length)
	{
		// https://tools.ietf.org/html/rfc8489#section-14.10
		// The NONCE attribute MUST be fewer than 128 characters
		OV_ASSERT2(length < 128);
	}
};
