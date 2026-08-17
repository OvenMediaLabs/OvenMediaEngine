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

class StunRealmAttribute : public StunTextAttributeFormat
{
public:
	StunRealmAttribute() : StunRealmAttribute(0){}
	StunRealmAttribute(int length) : StunTextAttributeFormat(StunAttributeType::Realm, length)
	{
		// https://tools.ietf.org/html/rfc8489#section-14.9
		// It MUST be a UTF-8-encoded [RFC3629] sequence of fewer than 128 characters
		OV_ASSERT2(length < 128);
	}
};
