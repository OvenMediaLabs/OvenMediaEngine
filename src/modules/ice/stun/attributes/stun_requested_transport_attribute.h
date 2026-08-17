//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2021 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include "stun_attribute.h"

#define REQUESTED_TRANSPORT_ATTRIBUTE_VALUE_LENGTH	4	// bytes

class StunRequestedTransportAttribute : public StunAttribute
{
public:
	StunRequestedTransportAttribute();
	StunRequestedTransportAttribute(int length);
	virtual ~StunRequestedTransportAttribute();

	bool Parse(const StunMessage *stun_message, ov::ByteStream &stream) override;

	uint8_t GetProtocolNumber() const;
	bool SetProtocolNumber(const uint8_t number);

	bool Serialize(const StunMessage *stun_message, ov::ByteStream &stream) const noexcept override;

	ov::String ToString() const override;

protected:
	// https://www.iana.org/assignments/protocol-numbers/protocol-numbers.xhtml
	// UDP : 11, OME only supports UDP 
	uint8_t	_protocol_number;
};
