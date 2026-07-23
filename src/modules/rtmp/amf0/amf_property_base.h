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

#include "./amf_define.h"

class AmfPropertyBase
{
public:
	AmfPropertyBase(AmfTypeMarker type);
	// copy ctor
	AmfPropertyBase(const AmfPropertyBase &other);
	AmfPropertyBase(AmfPropertyBase &&other) noexcept;

	AmfPropertyBase &operator=(const AmfPropertyBase &other);
	AmfPropertyBase &operator=(AmfPropertyBase &&other) noexcept;

	virtual ~AmfPropertyBase() = default;

	virtual bool Encode(ov::ByteStream &byte_stream, bool encode_marker) const = 0;
	virtual bool Decode(ov::ByteStream &byte_stream, bool decode_marker) = 0;

	virtual void ToString(ov::String &description, size_t indent = 0) const = 0;
	virtual ov::String ToString(size_t indent = 0) const = 0;

protected:
	static bool EncodeMarker(ov::ByteStream &byte_stream, AmfTypeMarker type);
	bool EncodeMarker(ov::ByteStream &byte_stream) const;

	static bool DecodeMarker(ov::ByteStream &byte_stream, bool peek, AmfTypeMarker *type);
	bool DecodeMarker(ov::ByteStream &byte_stream, bool peek);

protected:
	AmfTypeMarker _amf_data_type = AmfTypeMarker::Null;
};
