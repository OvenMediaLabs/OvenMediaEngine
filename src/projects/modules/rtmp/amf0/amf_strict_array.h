//==============================================================================
//
//  RTMPProvider
//
//  Created by Hyunjun Jang
//  Copyright (c) 2024 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include "./amf_object_array.h"

class AmfStrictArray : public AmfPropertyBase
{
public:
	AmfStrictArray();
	// copy ctor
	AmfStrictArray(const AmfStrictArray &other);
	// move ctor
	AmfStrictArray(AmfStrictArray &&other) noexcept;

	AmfStrictArray &operator=(const AmfStrictArray &other);
	AmfStrictArray &operator=(AmfStrictArray &&other) noexcept;

	bool Append(const AmfProperty &property);

	bool Encode(ov::ByteStream &byte_stream, bool encode_marker) const override;
	bool Decode(ov::ByteStream &byte_stream, bool decode_marker) override;

	const AmfProperty *GetAt(size_t index) const;

	void ToString(ov::String &description, size_t indent = 0) const override;
	ov::String ToString(size_t indent = 0) const override;

protected:
	std::vector<AmfProperty> _amf_properties;
};
