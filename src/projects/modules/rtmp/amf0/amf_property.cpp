//==============================================================================
//
//  RTMPProvider
//
//  Created by Hyunjun Jang
//  Copyright (c) 2024 AirenSoft. All rights reserved.
//
//==============================================================================
#include "./amf_property.h"

#include <string.h>

#include "../rtmp_private.h"
#include "./amf_object_array.h"
#include "./amf_strict_array.h"

AmfProperty::AmfProperty()
	: AmfProperty(AmfTypeMarker::Null)
{
}

AmfProperty::AmfProperty(AmfTypeMarker type)
	: AmfPropertyBase(type)
{
}

AmfProperty::AmfProperty(double number)
	: AmfPropertyBase(AmfTypeMarker::Number),
	  _number(number)
{
}

AmfProperty::AmfProperty(bool boolean)
	: AmfPropertyBase(AmfTypeMarker::Boolean),
	  _boolean(boolean)
{
}

AmfProperty::AmfProperty(const char *string)
	: AmfPropertyBase(AmfTypeMarker::String),
	  _string(string)
{
}

AmfProperty::AmfProperty(const AmfObject &object)
	: AmfPropertyBase(AmfTypeMarker::Object),
	  _object(new AmfObject(object))
{
}

AmfProperty::AmfProperty(const AmfEcmaArray &array)
	: AmfPropertyBase(AmfTypeMarker::EcmaArray),
	  _ecma_array(new AmfEcmaArray(array))
{
}

AmfProperty::AmfProperty(const AmfStrictArray &array)
	: AmfPropertyBase(AmfTypeMarker::StrictArray),
	  _strict_array(new AmfStrictArray(array))
{
}

AmfProperty::AmfProperty(const AmfProperty &other)
	: AmfPropertyBase(other._amf_data_type),
	  _number(other._number),
	  _boolean(other._boolean),
	  _string(other._string),
	  _object((other._object != nullptr) ? new AmfObject(*other._object) : nullptr),
	  _ecma_array((other._ecma_array != nullptr) ? new AmfEcmaArray(*(other._ecma_array)) : nullptr),
	  _strict_array((other._strict_array != nullptr) ? new AmfStrictArray(*(other._strict_array)) : nullptr)

{
}

AmfProperty::AmfProperty(AmfProperty &&other) noexcept
	: AmfPropertyBase(std::move(other))
{
	std::swap(_number, other._number);
	std::swap(_boolean, other._boolean);
	std::swap(_string, other._string);
	std::swap(_object, other._object);
	std::swap(_ecma_array, other._ecma_array);
	std::swap(_strict_array, other._strict_array);
}

AmfProperty &AmfProperty::operator=(const AmfProperty &other)
{
	if (this != &other)
	{
		Release();

		_amf_data_type = other._amf_data_type;
		_number = other._number;
		_boolean = other._boolean;

		_string = other._string;
		_object = (other._object != nullptr) ? new AmfObject(*other._object) : nullptr;
		_ecma_array = (other._ecma_array != nullptr) ? new AmfEcmaArray(*other._ecma_array) : nullptr;
		_strict_array = (other._strict_array != nullptr) ? new AmfStrictArray(*other._strict_array) : nullptr;
	}

	return *this;
}

AmfProperty &AmfProperty::operator=(AmfProperty &&other) noexcept
{
	if (this != &other)
	{
		std::swap(_amf_data_type, other._amf_data_type);
		std::swap(_number, other._number);
		std::swap(_boolean, other._boolean);
		std::swap(_string, other._string);
		std::swap(_object, other._object);
		std::swap(_ecma_array, other._ecma_array);
		std::swap(_strict_array, other._strict_array);
	}

	return *this;
}

AmfProperty::~AmfProperty()
{
	Release();
}

bool AmfProperty::EncodeNumber(ov::ByteStream &byte_stream) const
{
	// DOUBLE
	uint64_t number;
	::memcpy(&number, &_number, 8);

	return byte_stream.WriteBE64(number);
}

bool AmfProperty::EncodeBoolean(ov::ByteStream &byte_stream) const
{
	// U8 (0 is false, <> 0 is true)
	return byte_stream.Write8(_boolean ? 1 : 0);
}

bool AmfProperty::EncodeString(ov::ByteStream &byte_stream) const
{
	// UTF8-char = UTF8-1 | UTF8-2 | UTF8-3 | UTF8-4
	// UTF8-1 = %x00-7F
	// UTF8-2 = %xC2-DF UTF8-tail
	// UTF8-3 = %xE0 %xA0-BF UTF8-tail | %xE1-EC 2(UTF8-tail) |
	//			%xED %x80-9F UTF8-tail | %xEE-EF 2(UTF8-tail)
	// UTF8-4 = %xF0 %x90-BF 2(UTF8-tail) | %xF1-F3 3(UTF8-tail) |
	//			%xF4 %x80-8F 2(UTF8-tail)
	// UTF8-tail = %x80-BF

	// UTF-8 = U16 *(UTF8-char)
	// UTF-8-long = U32 *(UTF8-char)
	// UTF-8-empty = U16

	return
		// U16
		byte_stream.WriteBE16(static_cast<uint16_t>(_string.GetLength())) &&
		// UTF8-char
		byte_stream.Write(_string.CStr(), _string.GetLength());
}

bool AmfProperty::DecodeNumber(ov::ByteStream &byte_stream)
{
	if (byte_stream.IsRemained(8) == false)
	{
		logtt("Failed to decode number - not enough data (%zu bytes remained)", byte_stream.Remained());
		return false;
	}

	const auto data = byte_stream.ReadBE64();
	::memcpy(&_number, &data, 8);

	return true;
}

bool AmfProperty::DecodeBoolean(ov::ByteStream &byte_stream)
{
	if (byte_stream.IsRemained(1) == false)
	{
		logtt("Failed to decode boolean - not enough data (%zu bytes remained)", byte_stream.Remained());
		return false;
	}

	_boolean = (byte_stream.Read8() != 0) ? true : false;

	return true;
}

bool AmfProperty::DecodeString(ov::ByteStream &byte_stream)
{
	if (byte_stream.IsRemained(2) == false)
	{
		logtt("Failed to decode string - not enough data (%zu bytes remained)", byte_stream.Remained());
		return false;
	}

	auto length = byte_stream.ReadBE16();

	if (length == 0)
	{
		logtt("Empty string found");
		return true;
	}

	if (byte_stream.IsRemained(length) == false)
	{
		logtt("Invalid string length: %u (%zu bytes remained)", length, byte_stream.Remained());
		return false;
	}

	_string.SetLength(length);
	byte_stream.Read(_string.GetBuffer(), length);

	return true;
}

void AmfProperty::Release()
{
	OV_SAFE_DELETE(_ecma_array);
	OV_SAFE_DELETE(_strict_array);
	OV_SAFE_DELETE(_object);
}

bool AmfProperty::Encode(ov::ByteStream &byte_stream, bool encode_marker) const
{
	if (encode_marker && EncodeMarker(byte_stream) == false)
	{
		logtt("Failed to encode marker");
		return false;
	}

	auto succeeded = false;

	switch (_amf_data_type)
	{
		case AmfTypeMarker::Number:
			succeeded = EncodeNumber(byte_stream);
			break;

		case AmfTypeMarker::Boolean:
			succeeded = EncodeBoolean(byte_stream);
			break;

		case AmfTypeMarker::String:
			succeeded = (_string != nullptr) ? EncodeString(byte_stream) : false;
			break;

		case AmfTypeMarker::Object:
			succeeded = (_object != nullptr) ? _object->Encode(byte_stream, false) : 0;
			break;

		case AmfTypeMarker::Null:
			[[fallthrough]];
		case AmfTypeMarker::Undefined:
			succeeded = true;
			break;

		case AmfTypeMarker::EcmaArray:
			succeeded = (_ecma_array != nullptr) ? _ecma_array->Encode(byte_stream, false) : 0;
			break;

		case AmfTypeMarker::StrictArray:
			succeeded = (_strict_array != nullptr) ? _strict_array->Encode(byte_stream, false) : false;
			break;

		default:
			break;
	}

	return succeeded;
}

bool AmfProperty::Decode(ov::ByteStream &byte_stream, bool decode_marker)
{
	if (decode_marker)
	{
		if (byte_stream.IsRemained(1) == false)
		{
			OV_ASSERT2(byte_stream.IsRemained(1));
			return false;
		}

		if (DecodeMarker(byte_stream, false) == false)
		{
			logtt("Failed to decode marker");
			return false;
		}
	}

	switch (_amf_data_type)
	{
		case AmfTypeMarker::Number:
			return DecodeNumber(byte_stream);

		case AmfTypeMarker::Boolean:
			return DecodeBoolean(byte_stream);

		case AmfTypeMarker::String:
			return DecodeString(byte_stream);

		case AmfTypeMarker::Object:
			OV_SAFE_DELETE(_object);
			_object = new AmfObject();
			if (_object->Decode(byte_stream, false))
			{
				_amf_data_type = AmfTypeMarker::Object;
				return true;
			}

			OV_SAFE_DELETE(_object);
			break;

		case AmfTypeMarker::Null:
			[[fallthrough]];
		case AmfTypeMarker::Undefined:
			return true;

		case AmfTypeMarker::EcmaArray:
			OV_SAFE_DELETE(_ecma_array);
			_ecma_array = new AmfEcmaArray();
			if (_ecma_array->Decode(byte_stream, false))
			{
				return true;
			}

			OV_SAFE_DELETE(_ecma_array);
			break;

		case AmfTypeMarker::StrictArray:
			OV_SAFE_DELETE(_strict_array);
			_strict_array = new AmfStrictArray();
			if (_strict_array->Decode(byte_stream, false))
			{
				_amf_data_type = AmfTypeMarker::StrictArray;
				return true;
			}

			OV_SAFE_DELETE(_strict_array);
			break;

		default:
			logtw("Unknown AMF type: %d", _amf_data_type);
			_amf_data_type = AmfTypeMarker::Null;
			break;
	}

	return false;
}

void AmfProperty::ToString(ov::String &description, size_t indent) const
{
	switch (GetType())
	{
		case AmfTypeMarker::Number:
			description.AppendFormat("%.1f\n", GetNumber());
			break;

		case AmfTypeMarker::Boolean:
			description.AppendFormat("%s\n", GetBoolean() ? "true" : "false");
			break;

		case AmfTypeMarker::String:
			if (_string != nullptr)
			{
				description.AppendFormat("\"%s\"\n", GetString().CStr());
			}
			else
			{
				description.Append("<null-string>\n");
			}
			break;

		case AmfTypeMarker::Object:
			if (_object != nullptr)
			{
				_object->ToString(description, indent);
			}
			else
			{
				description.Append("<null-object>\n");
			}

			break;

		case AmfTypeMarker::Null:
			description.Append("<null>\n");
			break;

		case AmfTypeMarker::Undefined:
			description.Append("<undefined>\n");
			break;

		case AmfTypeMarker::EcmaArray:
			if (_ecma_array != nullptr)
			{
				_ecma_array->ToString(description, indent);
			}
			else
			{
				description.Append("<null-ecma-array>\n");
			}
			break;

		case AmfTypeMarker::StrictArray:
			if (_strict_array != nullptr)
			{
				_strict_array->ToString(description, indent);
			}
			else
			{
				description.Append("<null-strict-array>\n");
			}
			break;

		case AmfTypeMarker::MovieClip:
			[[fallthrough]];
		case AmfTypeMarker::Reference:
			[[fallthrough]];
		case AmfTypeMarker::ObjectEnd:
			[[fallthrough]];
		case AmfTypeMarker::Date:
			[[fallthrough]];
		case AmfTypeMarker::LongString:
			[[fallthrough]];
		case AmfTypeMarker::Unsupported:
			[[fallthrough]];
		case AmfTypeMarker::Recordset:
			[[fallthrough]];
		case AmfTypeMarker::Xml:
			[[fallthrough]];
		case AmfTypeMarker::TypedObject:
			OV_ASSERT(false, "Not supported type: ", GetType());
			break;
	}
}

ov::String AmfProperty::ToString(size_t indent) const
{
	ov::String description;
	ToString(description, indent);
	return description;
}
