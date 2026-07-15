//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include "templates/stun_address_attribute_format.h"

// RFC5389
//
// 15.1.  MAPPED-ADDRESS
//
//    The MAPPED-ADDRESS attribute indicates a reflexive transport address
//    of the client.  It consists of an 8-bit address family and a 16-bit
//    port, followed by a fixed-length value representing the IP address.
//    If the address family is IPv4, the address MUST be 32 bits.  If the
//    address family is IPv6, the address MUST be 128 bits.  All fields
//    must be in network byte order.
//
//    The format of the MAPPED-ADDRESS attribute is:
//
//        0                   1                   2                   3
//        0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
//       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//       |0 0 0 0 0 0 0 0|    Family     |           Port                |
//       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//       |                                                               |
//       |                 Address (32 bits or 128 bits)                 |
//       |                                                               |
//       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//
//                Figure 5: Format of MAPPED-ADDRESS Attribute
//
//    The address family can take on the following values:
//
//    0x01:IPv4
//    0x02:IPv6
//
//    The first 8 bits of the MAPPED-ADDRESS MUST be set to 0 and MUST be
//    ignored by receivers.  These bits are present for aligning parameters
//    on natural 32-bit boundaries.
//
//    This attribute is used only by servers for achieving backwards
//    compatibility with RFC 3489 [RFC3489] clients.
class StunMappedAddressAttribute : public StunAddressAttributeFormat
{
public:
	StunMappedAddressAttribute()
		: StunMappedAddressAttribute(0) {}
	StunMappedAddressAttribute(int length)
		: StunAddressAttributeFormat(StunAttributeType::MappedAddress, length) {}
};
