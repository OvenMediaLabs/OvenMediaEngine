//==============================================================================
//
//  RTMPProvider
//
//  Created by Hyunjun Jang
//  Copyright (c) 2024 AirenSoft. All rights reserved.
//
//==============================================================================
#include "./amf_property_base.h"

AmfPropertyBase::AmfPropertyBase(AmfTypeMarker type)
	: _amf_data_type(type)
{
}

AmfPropertyBase::AmfPropertyBase(const AmfPropertyBase &other)
	: _amf_data_type(other._amf_data_type)
{
}

AmfPropertyBase::AmfPropertyBase(AmfPropertyBase &&other) noexcept
{
	std::swap(_amf_data_type, other._amf_data_type);
}

AmfPropertyBase &AmfPropertyBase::operator=(const AmfPropertyBase &other)
{
	if (this != &other)
	{
		_amf_data_type = other._amf_data_type;
	}

	return *this;
}

AmfPropertyBase &AmfPropertyBase::operator=(AmfPropertyBase &&other) noexcept
{
	if (this != &other)
	{
		std::swap(_amf_data_type, other._amf_data_type);
	}

	return *this;
}

bool AmfPropertyBase::EncodeMarker(ov::ByteStream &byte_stream, AmfTypeMarker type)
{
	return byte_stream.Write8(static_cast<uint8_t>(type));
}

bool AmfPropertyBase::EncodeMarker(ov::ByteStream &byte_stream) const
{
	return EncodeMarker(byte_stream, _amf_data_type);
}

bool AmfPropertyBase::DecodeMarker(ov::ByteStream &byte_stream, bool peek, AmfTypeMarker *type)
{
	if (byte_stream.IsRemained(1) == false)
	{
		return false;
	}

	auto peeks = byte_stream.Peek<uint8_t>();
	*type = static_cast<AmfTypeMarker>(peek ? peeks : byte_stream.Read8());

	return true;
}

bool AmfPropertyBase::DecodeMarker(ov::ByteStream &byte_stream, bool peek)
{
	return DecodeMarker(byte_stream, peek, &_amf_data_type);
}