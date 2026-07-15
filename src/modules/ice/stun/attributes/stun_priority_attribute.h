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

class StunPriorityAttribute : public StunOctetAttributeFormat<uint32_t>
{
public:
	StunPriorityAttribute():StunPriorityAttribute(sizeof(uint32_t)){}
	StunPriorityAttribute(int length):StunOctetAttributeFormat(StunAttributeType::Priority, length){}
};