//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include "stun_address_attribute_format.h"

//
// RFC 5389
//
// 15.2.  XOR-MAPPED-ADDRESS
//
//    The XOR-MAPPED-ADDRESS attribute is identical to the MAPPED-ADDRESS
//    attribute, except that the reflexive transport address is obfuscated
//    through the XOR function.
//
//    The format of the XOR-MAPPED-ADDRESS is:
//
//       0                   1                   2                   3
//       0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
//      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//      |x x x x x x x x|    Family     |         X-Port                |
//      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//      |                X-Address (Variable)
//      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//
//              Figure 6: Format of XOR-MAPPED-ADDRESS Attribute
//
//    The Family represents the IP address family, and is encoded
//    identically to the Family in MAPPED-ADDRESS.
//    X-Port is computed by taking the mapped port in host byte order,
//    XOR'ing it with the most significant 16 bits of the magic cookie, and
//    then the converting the result to network byte order.  If the IP
//    address family is IPv4, X-Address is computed by taking the mapped IP
//    address in host byte order, XOR'ing it with the magic cookie, and
//    converting the result to network byte order.  If the IP address
//    family is IPv6, X-Address is computed by taking the mapped IP address
//    in host byte order, XOR'ing it with the concatenation of the magic
//    cookie and the 96-bit transaction ID, and converting the result to
//    network byte order.
//
//    The rules for encoding and processing the first 8 bits of the
//    attribute's value, the rules for handling multiple occurrences of the
//    attribute, and the rules for processing address families are the same
//    as for MAPPED-ADDRESS.
//
//    Note: XOR-MAPPED-ADDRESS and MAPPED-ADDRESS differ only in their
//    encoding of the transport address.  The former encodes the transport
//    address by exclusive-or'ing it with the magic cookie.  The latter
//    encodes it directly in binary.  RFC 3489 originally specified only
//    MAPPED-ADDRESS.  However, deployment experience found that some NATs
//    rewrite the 32-bit binary payloads containing the NAT's public IP
//    address, such as STUN's MAPPED-ADDRESS attribute, in the well-meaning
//    but misguided attempt at providing a generic ALG function.  Such
//    behavior interferes with the operation of STUN and also causes
//    failure of STUN's message-integrity checking.
class StunXorAddressAttributeFormat : public StunAddressAttributeFormat
{
public:
	bool Parse(const StunMessage *stun_message, ov::ByteStream &stream) override;
	bool Serialize(const StunMessage *stun_message, ov::ByteStream &stream) const noexcept override;
	ov::String ToString() const override;

protected:
	StunXorAddressAttributeFormat(StunAttributeType type, int length);

	bool SerializeXoredAddress(const StunMessage *stun_message, ov::ByteStream &stream) const noexcept;
};