//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2021 AirenSoft. All rights reserved.
//
//==============================================================================

#pragma once
#include <base/ovlibrary/ovlibrary.h>

enum class RtspHeaderFieldType : uint16_t
{
	Unknown = 0,
	CSeq,
	UserAgent,
	Server,
	CacheControl,
	Public,
	Unsupported,
	Accept,
	ContentLength,
	ContentBase,
	Date,
	ContentType,
	Session,
	Transport,
	Expires,
	Range,
	WWWAuthenticate,
	Authorization
};

// Basic field(name, value) class
class RtspHeaderField
{
public:
	virtual ~RtspHeaderField() = default;
	RtspHeaderField();
	RtspHeaderField(RtspHeaderFieldType type);
	RtspHeaderField(RtspHeaderFieldType type, ov::String value);
	RtspHeaderField(RtspHeaderFieldType type, int32_t value);
	RtspHeaderField(ov::String name, ov::String value);
	RtspHeaderField(ov::String name, int32_t value);

	virtual bool Parse(const ov::String &message);
	
	virtual ov::String Serialize() const;

	virtual bool SetContent(RtspHeaderFieldType type, ov::String value);
	virtual bool SetContent(ov::String name, ov::String value);
	virtual bool SetContent(ov::String name, int32_t value);
	virtual bool SetValue(ov::String value);
	
	ov::String GetName();
	ov::String GetValue();
	int32_t GetValueAsInteger();

	static ov::String FieldTypeToString(RtspHeaderFieldType type);

protected:
	ov::String	_name;

private:
	// If it is a known meesage, it can be casted to that MessageHeader
	RtspHeaderFieldType _type = RtspHeaderFieldType::Unknown;
	ov::String	_value;
};