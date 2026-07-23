//==============================================================================
//
//  RTMPProvider
//
//  Created by Hyunjun Jang
//  Copyright (c) 2024 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <stdint.h>
#include <string.h>

#include <vector>

#include "./amf_object_array.h"
#include "./amf_property.h"

class AmfDocument
{
public:
	bool Encode(ov::ByteStream &byte_stream) const;
	bool Decode(ov::ByteStream &byte_stream);

	bool AppendProperty(const AmfProperty &property);

	size_t GetPropertyCount() const
	{
		return _amf_properties.size();
	}

	AmfProperty *GetProperty(size_t index);
	const AmfProperty *GetProperty(size_t index) const;
	const AmfProperty *GetProperty(size_t index, AmfTypeMarker expected_type) const;

	ov::String ToString(int indent = 0) const;

private:
	std::vector<AmfProperty> _amf_properties;
};
