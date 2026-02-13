//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include "modules/ice/stun/attributes/stun_attribute.h"
#include "modules/ice/stun/stun_datastructure.h"

class StunAddressAttributeFormat : public StunAttribute
{
public:
	bool Parse(const StunMessage *stun_message, ov::ByteStream &stream) override;

	StunAddressFamily GetFamily() const;
	uint16_t GetPort() const;
	const ov::SocketAddress &GetAddress() const;
	bool SetParameters(const ov::SocketAddress &address);
	bool Serialize(const StunMessage *stun_message, ov::ByteStream &stream) const noexcept override;
	ov::String ToString() const override;

protected:
	StunAddressAttributeFormat(StunAttributeType type, int length);

	ov::String ToString(const char *class_name) const;
	ov::SocketAddress _address;
};
