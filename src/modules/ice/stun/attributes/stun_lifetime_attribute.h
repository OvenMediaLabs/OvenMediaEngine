//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2021 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include "templates/stun_octet_attribute_format.h"
#include "modules/ice/stun/stun_datastructure.h"

class StunLifetimeAttribute : public StunOctetAttributeFormat<uint32_t>
{
public:
	StunLifetimeAttribute():StunLifetimeAttribute(sizeof(uint32_t)){}
	StunLifetimeAttribute(int length):StunOctetAttributeFormat(StunAttributeType::Lifetime, length){}
};