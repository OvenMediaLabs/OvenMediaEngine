//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2021 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include "templates/stun_xor_address_attribute_format.h"

// XOR-PEER-ADDRESS and XOR-RELAYED-ADDRESS are encoded in the same way as the this
class StunXorPeerAddressAttribute : public StunXorAddressAttributeFormat
{
public:
	StunXorPeerAddressAttribute():StunXorPeerAddressAttribute(0){}
	StunXorPeerAddressAttribute(int length):StunXorAddressAttributeFormat(StunAttributeType::XorPeerAddress, length){}
};
