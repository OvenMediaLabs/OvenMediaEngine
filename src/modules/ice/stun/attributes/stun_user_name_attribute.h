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

class StunUserNameAttribute : public StunTextAttributeFormat
{
public:
	StunUserNameAttribute() : StunUserNameAttribute(0){}
	StunUserNameAttribute(int length) : StunTextAttributeFormat(StunAttributeType::UserName, length)
	{
		// RFC5389 - 15.3. USERNAME
		// The value of USERNAME is a variable-length value.  It MUST contain a
		// UTF-8 [RFC3629] encoded sequence of less than 513 bytes, and MUST
		// have been processed using SASLprep [RFC4013].

		OV_ASSERT2(length < 513);
	}
};
