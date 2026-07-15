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

#include <string>

#include "./amf_property_base.h"

class AmfEcmaArray;
class AmfStrictArray;
class AmfObject;

class AmfProperty : public AmfPropertyBase
{
public:
	// The same as AmfProperty::NullProperty()
	AmfProperty();
	AmfProperty(AmfTypeMarker type);
	AmfProperty(double number);
	AmfProperty(bool boolean);
	AmfProperty(const char *string);
	AmfProperty(const AmfObject &object);
	AmfProperty(const AmfEcmaArray &array);
	AmfProperty(const AmfStrictArray &array);

	// copy ctor
	AmfProperty(const AmfProperty &other);
	// move ctor
	AmfProperty(AmfProperty &&other) noexcept;

	AmfProperty &operator=(const AmfProperty &other);
	AmfProperty &operator=(AmfProperty &&other) noexcept;

	~AmfProperty() override;

	static AmfProperty NullProperty()
	{
		return AmfProperty(AmfTypeMarker::Null);
	}

	static AmfProperty UndefinedProperty()
	{
		return AmfProperty(AmfTypeMarker::Undefined);
	}

	bool Encode(ov::ByteStream &byte_stream, bool encode_marker) const override;
	bool Decode(ov::ByteStream &byte_stream, bool decode_marker) override;

	AmfTypeMarker GetType() const
	{
		return _amf_data_type;
	}

	double GetNumber() const
	{
		return _number;
	}

	bool GetBoolean() const
	{
		return _boolean;
	}

	ov::String GetString() const
	{
		return _string;
	}

	const AmfObject *GetObject() const
	{
		return _object;
	}

	const AmfEcmaArray *GetEcmaArray() const
	{
		return _ecma_array;
	}

	const AmfStrictArray *GetStrictArray() const
	{
		return _strict_array;
	}

	void ToString(ov::String &description, size_t indent = 0) const override;
	ov::String ToString(size_t indent = 0) const override;

protected:
	bool EncodeNumber(ov::ByteStream &byte_stream) const;
	bool EncodeBoolean(ov::ByteStream &byte_stream) const;
	bool EncodeString(ov::ByteStream &byte_stream) const;

	bool DecodeNumber(ov::ByteStream &byte_stream);
	bool DecodeBoolean(ov::ByteStream &byte_stream);
	bool DecodeString(ov::ByteStream &byte_stream);

	void Release();

protected:
	double _number = 0.0;
	bool _boolean = false;
	ov::String _string;
	AmfObject *_object = nullptr;
	AmfEcmaArray *_ecma_array = nullptr;
	AmfStrictArray *_strict_array = nullptr;
};

struct AmfPropertyPair
{
	ov::String name;
	AmfProperty property;

	AmfPropertyPair(const ov::String &name)
		: name(name),
		  property(AmfTypeMarker::Null)
	{
	}

	template <typename T>
	AmfPropertyPair(const ov::String &name, T &value)
		: name(name),
		  property(value)
	{
	}

	AmfPropertyPair(const ov::String &name, AmfTypeMarker type)
		: name(name),
		  property(type)
	{
	}

	bool IsTypeOf(AmfTypeMarker type) const
	{
		return (property.GetType() == type);
	}
};
