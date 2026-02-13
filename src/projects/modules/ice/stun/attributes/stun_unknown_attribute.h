//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include "stun_attribute.h"

class StunUnknownAttribute : public StunAttribute
{
public:
	StunUnknownAttribute(int type, int length);
	virtual ~StunUnknownAttribute();

	bool Parse(const StunMessage *stun_message, ov::ByteStream &stream) override;

	bool SetData(const void *data, int length);

	bool Serialize(const StunMessage *stun_message, ov::ByteStream &stream) const noexcept override;

	ov::String ToString() const override;

private:
	std::shared_ptr<ov::Data> _data;
};

