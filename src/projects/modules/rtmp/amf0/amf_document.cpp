//==============================================================================
//
//  RTMPProvider
//
//  Created by Hyunjun Jang
//  Copyright (c) 2024 AirenSoft. All rights reserved.
//
//==============================================================================
#include "amf_document.h"

#include "providers/rtmp/rtmp_provider_private.h"

bool AmfDocument::Encode(ov::ByteStream &byte_stream) const
{
	for (auto &property : _amf_properties)
	{
		if (property.Encode(byte_stream, true) == false)
		{
			return false;
		}
	}

	return true;
}

bool AmfDocument::Decode(ov::ByteStream &byte_stream)
{
	while (byte_stream.IsEmpty() == false)
	{
		AmfProperty property;
		if (property.Decode(byte_stream, true) == false)
		{
			return false;
		}

		_amf_properties.push_back(std::move(property));
	}

	return true;
}

bool AmfDocument::AppendProperty(const AmfProperty &property)
{
	_amf_properties.push_back(property);

	return true;
}

AmfProperty *AmfDocument::GetProperty(size_t index)
{
	return (index < _amf_properties.size()) ? &(_amf_properties[index]) : nullptr;
}

const AmfProperty *AmfDocument::GetProperty(size_t index) const
{
	return (index < _amf_properties.size()) ? &(_amf_properties[index]) : nullptr;
}

const AmfProperty *AmfDocument::GetProperty(size_t index, AmfTypeMarker expected_type) const
{
	auto property = GetProperty(index);

	if ((property != nullptr) && (property->GetType() == expected_type))
	{
		return property;
	}

	return nullptr;
}

ov::String AmfDocument::ToString(int indent) const
{
	ov::String description;
	for (auto &property : _amf_properties)
	{
		property.ToString(description, indent);
	}
	return description;
}