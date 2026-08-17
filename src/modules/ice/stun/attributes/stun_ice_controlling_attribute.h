//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2023 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include "templates/stun_octet_attribute_format.h"
#include "modules/ice/stun/stun_datastructure.h"

class StunIceControllingAttribute : public StunOctetAttributeFormat<uint64_t>
{
public:
	StunIceControllingAttribute():StunIceControllingAttribute(sizeof(uint64_t)){}
	StunIceControllingAttribute(int length):StunOctetAttributeFormat(StunAttributeType::IceControlling, length){}
};