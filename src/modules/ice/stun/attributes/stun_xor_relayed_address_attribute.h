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
class StunXorRelayedAddressAttribute : public StunXorAddressAttributeFormat
{
public:
	StunXorRelayedAddressAttribute():StunXorRelayedAddressAttribute(0){}
	StunXorRelayedAddressAttribute(int length):StunXorAddressAttributeFormat(StunAttributeType::XorRelayedAddress, length){}
};
