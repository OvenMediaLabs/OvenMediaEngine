//==============================================================================
//
//  RTMPProvider
//
//  Created by Hyunjun Jang
//  Copyright (c) 2024 AirenSoft. All rights reserved.
//
//==============================================================================
#include "./amf_object_array.h"

#include <cstdlib>
#include <cstring>

#include "providers/rtmp/rtmp_provider_private.h"

AmfObjectArray::AmfObjectArray(AmfTypeMarker type)
	: AmfPropertyBase(type)
{
}

AmfObjectArray::AmfObjectArray(const AmfObjectArray &other)
	: AmfPropertyBase(other)
{
	_amf_property_pairs = other._amf_property_pairs;
}

AmfObjectArray::AmfObjectArray(AmfObjectArray &&other) noexcept
	: AmfPropertyBase(std::move(other))
{
	std::swap(_amf_property_pairs, other._amf_property_pairs);
}

AmfObjectArray &AmfObjectArray::operator=(const AmfObjectArray &other)
{
	if (this != &other)
	{
		AmfPropertyBase::operator=(other);
		_amf_property_pairs = other._amf_property_pairs;
	}

	return *this;
}

AmfObjectArray &AmfObjectArray::operator=(AmfObjectArray &&other) noexcept
{
	if (this != &other)
	{
		AmfPropertyBase::operator=(std::move(other));
		std::swap(_amf_property_pairs, other._amf_property_pairs);
	}

	return *this;
}

bool AmfObjectArray::Append(const char *name, const AmfProperty &property)
{
	if (name == nullptr)
	{
		logtt("Could not append the property - name is null");
		return false;
	}

	_amf_property_pairs.emplace_back(name, property);
	return true;
}

bool AmfObjectArray::Encode(ov::ByteStream &byte_stream, bool encode_marker) const
{
	if (encode_marker && (EncodeMarker(byte_stream) == false))
	{
		logtt("Failed to encode marker");
		return false;
	}

	// Write the count of items if this is an ECMA array
	if (_amf_data_type == AmfTypeMarker::EcmaArray)
	{
		byte_stream.WriteBE32(_amf_property_pairs.size());
	}

	for (const auto &property_pair : _amf_property_pairs)
	{
		// Encode the name
		byte_stream.WriteBE16(property_pair.name.GetLength());
		byte_stream.Write(property_pair.name.CStr(), property_pair.name.GetLength());

		// Encode the value
		if (property_pair.property.Encode(byte_stream, true) == false)
		{
			return false;
		}
	}

	// (UTF-8-empty object-end-marker)
	return byte_stream.WriteBE16(0) && EncodeMarker(byte_stream, AmfTypeMarker::ObjectEnd);
}

bool AmfObjectArray::Decode(ov::ByteStream &byte_stream, bool decode_marker)
{
	// 1. type == AmfTypeMarker::Object
	//
	// object-property			= 	(UTF-8 value-type) |
	// 								(UTF-8-empty object-end-marker)
	// anonymous-object-type	=	object-marker *(object-property)
	//
	// 2. type == AmfTypeMarker::EcmaArray
	//
	// associative-count		=	U32
	// ecma-array-type			=	associative-count *(object-property)
	if (decode_marker)
	{
		AmfTypeMarker type;
		if (DecodeMarker(byte_stream, false, &type) == false)
		{
			logtt("Failed to decode marker");
			return false;
		}

		// AmfObjectArray can be either an object or an ECMA array, so we need to check the type
		if (type != _amf_data_type)
		{
			OV_ASSERT(type == _amf_data_type, "Type mismatch: expected: %d, actual: %d", ov::ToUnderlyingType(_amf_data_type), ov::ToUnderlyingType(type));
			return false;
		}
	}

	if (_amf_data_type == AmfTypeMarker::EcmaArray)
	{
		if (byte_stream.IsRemained(4) == false)
		{
			logtt("Failed to read the count of items");
			return false;
		}

		// Ignore the count of items (we don't need it)
		byte_stream.ReadBE32();
	}

	AmfTypeMarker type;

	while (byte_stream.IsEmpty() == false)
	{
		// Read the length of the name (U16)
		const auto name_length = byte_stream.ReadBE16();

		if (name_length == 0)
		{
			// Maybe the object-end-marker, so we need to check the end marker to check if we have reached the end of the array/object
			if (DecodeMarker(byte_stream, true, &type) && (type == AmfTypeMarker::ObjectEnd))
			{
				// End marker found
				byte_stream.Skip(1);
				break;
			}

			// Ignore the empty name
			continue;
		}

		if (byte_stream.IsRemained(name_length) == false)
		{
			logtt("Failed to read the name");
			return false;
		}

		// Read the name
		ov::String name;
		name.SetLength(name_length);
		byte_stream.Read(name.GetBuffer(), name_length);

		// Read the value
		AmfProperty property;
		if (property.Decode(byte_stream, true) == false)
		{
			// Could not decode the data
			logtt("Failed to decode the value");
			return false;
		}

		Append(name, property);
	}

	return true;
}

const AmfPropertyPair *AmfObjectArray::GetPair(const char *name) const
{
	if (name != nullptr)
	{
		for (auto &property_pair : _amf_property_pairs)
		{
			if (property_pair.name == name)
			{
				return &property_pair;
			}
		}
	}

	return nullptr;
}

const AmfPropertyPair *AmfObjectArray::GetPair(const char *name, AmfTypeMarker expected_type) const
{
	auto property_pair = GetPair(name);

	if ((property_pair != nullptr) && property_pair->IsTypeOf(expected_type))
	{
		return property_pair;
	}

	return nullptr;
}

const std::optional<double> AmfObjectArray::GetDoubleValue(const char *name) const
{
	auto pair = GetPair(name, AmfTypeMarker::Number);

	if (pair != nullptr)
	{
		return pair->property.GetNumber();
	}

	return std::nullopt;
}

const std::optional<ov::String> AmfObjectArray::GetStringValue(const char *name) const
{
	auto pair = GetPair(name, AmfTypeMarker::String);

	if (pair != nullptr)
	{
		return pair->property.GetString();
	}

	return std::nullopt;
}

void AmfObjectArray::ToString(ov::String &description, size_t indent) const
{
	auto indent_string = ov::String::Repeat("    ", indent);
	auto item_indent_string = ov::String::Repeat("    ", indent + 1);
	auto start_char = (_amf_data_type == AmfTypeMarker::Object) ? '{' : '[';
	auto end_char = (_amf_data_type == AmfTypeMarker::Object) ? '}' : ']';

	if (_amf_property_pairs.empty())
	{
		description.AppendFormat("%c%c\n", start_char, end_char);
		return;
	}

	description.AppendFormat("%c\n", start_char);

	for (auto &property_pair : _amf_property_pairs)
	{
		description.AppendFormat("%s%s: ", item_indent_string.CStr(), property_pair.name.CStr());

		property_pair.property.ToString(
			description,
			(property_pair.IsTypeOf(AmfTypeMarker::Object) ||
			 property_pair.IsTypeOf(AmfTypeMarker::EcmaArray))
				? indent + 1
				: 0);
	}

	description.AppendFormat("%s%c\n", indent_string.CStr(), end_char);
}

ov::String AmfObjectArray::ToString(size_t indent) const
{
	ov::String description;
	ToString(description, indent);
	return description;
}

std::vector<AmfPropertyPair> AmfObjectArray::GetPropertyPairs() const
{
	return _amf_property_pairs;
}