//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2022 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include "../id3v2_frame.h"

class ID3v2TxxxFrame : public ID3v2Frame
{
public:
	ID3v2TxxxFrame(const ov::String &description, const ov::String &value)
	{
		SetFrameId("TXXX");
		SetDescription(description);
		SetValue(value);
	}

	virtual ~ID3v2TxxxFrame(){}
	
	void SetDescription(const ov::String &description)
	{
		_description = description;
	}

	void SetValue(const ov::String &value)
	{
		_value = value;
	}

	const ov::String &GetDescription() const
	{
		return _description;
	}

	const ov::String &GetValue() const
	{
		return _value;
	}

	std::shared_ptr<ov::Data> GetData() const override
	{
		// Text encoding $xx + Description<text string according to encoding> + $00 + Value<text string according to encoding>
		ov::ByteStream stream(1 + _description.GetLength() + 1 + _value.GetLength());

		stream.WriteBE(_encoding);
		stream.WriteText(_description, true);
		stream.WriteText(_value, false);

		return stream.GetDataPointer();
	}

private:
	// $00   ISO-8859-1 [ISO-8859-1]. Terminated with $00.
    // $01   UTF-16 [UTF-16] encoded Unicode [UNICODE] with BOM. All
    //       strings in the same frame SHALL have the same byteorder.
    //       Terminated with $00 00.
    // $02   UTF-16BE [UTF-16] encoded Unicode [UNICODE] without BOM.
    //       Terminated with $00 00.
    // $03   UTF-8 [UTF-8] encoded Unicode [UNICODE]. Terminated with $00.
	uint8_t _encoding = 0x03; // only support UTF-8
	ov::String _description;
	ov::String _value;
};