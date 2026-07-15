//==============================================================================
//
//  RTMPProvider
//
//  Created by Hyunjun Jang
//  Copyright (c) 2024 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <base/ovlibrary/ovlibrary.h>
#include <string.h>

#include <vector>

#include "./amf_property.h"

// Key/Value based object
class AmfObjectArray : AmfPropertyBase
{
public:
	AmfObjectArray(AmfTypeMarker type);
	// copy ctor
	AmfObjectArray(const AmfObjectArray &other);
	// move ctor
	AmfObjectArray(AmfObjectArray &&other) noexcept;

	AmfObjectArray &operator=(const AmfObjectArray &other);
	AmfObjectArray &operator=(AmfObjectArray &&other) noexcept;

	bool Append(const char *name, const AmfProperty &property);

	bool Encode(ov::ByteStream &byte_stream, bool encode_marker) const override;
	bool Decode(ov::ByteStream &byte_stream, bool decode_marker) override;

	const AmfPropertyPair *GetPair(const char *name) const;
	const AmfPropertyPair *GetPair(const char *name, AmfTypeMarker expected_type) const;

	const std::optional<double> GetDoubleValue(const char *name) const;
	const std::optional<ov::String> GetStringValue(const char *name) const;

	void ToString(ov::String &description, size_t indent = 0) const override;
	ov::String ToString(size_t indent = 0) const override;

	std::vector<AmfPropertyPair> GetPropertyPairs() const;
	
protected:
	std::vector<AmfPropertyPair> _amf_property_pairs;
};

class AmfObject : public AmfObjectArray
{
public:
	AmfObject()
		: AmfObjectArray(AmfTypeMarker::Object) {}
};

class AmfEcmaArray : public AmfObjectArray
{
public:
	AmfEcmaArray()
		: AmfObjectArray(AmfTypeMarker::EcmaArray) {}
};
