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

class StunIceControlledAttribute : public StunOctetAttributeFormat<uint64_t>
{
public:
	StunIceControlledAttribute():StunIceControlledAttribute(sizeof(uint64_t)){}
	StunIceControlledAttribute(int length):StunOctetAttributeFormat(StunAttributeType::IceControlled, length){}
};