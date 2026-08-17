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

class StunDataAttribute : public StunAttribute
{
public:
	StunDataAttribute();
	StunDataAttribute(int length);
	virtual ~StunDataAttribute();

	bool Parse(const StunMessage *stun_message, ov::ByteStream &stream) override;

	const std::shared_ptr<const ov::Data>& GetData() const;
	bool SetData(const std::shared_ptr<const ov::Data> &data);

	bool Serialize(const StunMessage *stun_message, ov::ByteStream &stream) const noexcept override;

	ov::String ToString() const override;

protected:
	std::shared_ptr<const ov::Data> _data = nullptr;
};
