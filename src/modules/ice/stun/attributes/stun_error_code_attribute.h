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
#include "modules/ice/stun/stun_datastructure.h"

class StunErrorCodeAttribute : public StunAttribute
{
public:
	StunErrorCodeAttribute(int length);
	// Length will be set when SetError or Parse function called
	StunErrorCodeAttribute();

	virtual bool Parse(const StunMessage *stun_message, ov::ByteStream &stream) override;
	
	// If extra reason is empty, the default error message is used.
	bool SetError(StunErrorCode error_code, ov::String extra_reason = "");

	uint8_t GetErrorCodeClass() const;
	uint8_t GetErrorCodeNumber() const;
	StunErrorCode GetErrorCode() const;
	const ov::String& GetErrorReason() const;

	bool Serialize(const StunMessage *stun_message, ov::ByteStream &stream) const noexcept override;
	virtual ov::String ToString() const override;

private:
	StunErrorCode	_code;
	ov::String		_reason;
};
