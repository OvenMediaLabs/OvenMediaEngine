//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include "templates/stun_octet_attribute_format.h"
#include "modules/ice/stun/stun_datastructure.h"
class StunFingerprintAttribute : public StunOctetAttributeFormat<uint32_t>
{
public:
	StunFingerprintAttribute():StunFingerprintAttribute(sizeof(uint32_t)){}
	StunFingerprintAttribute(int length):StunOctetAttributeFormat(StunAttributeType::Fingerprint, length){}
};