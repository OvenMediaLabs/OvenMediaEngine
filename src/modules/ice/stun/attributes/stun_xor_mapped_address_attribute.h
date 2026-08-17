//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include "templates/stun_xor_address_attribute_format.h"

class StunXorMappedAddressAttribute : public StunXorAddressAttributeFormat
{
public:
	StunXorMappedAddressAttribute()
		: StunXorMappedAddressAttribute(0) {}
	StunXorMappedAddressAttribute(int length)
		: StunXorAddressAttributeFormat(StunAttributeType::XorMappedAddress, length) {}
};
