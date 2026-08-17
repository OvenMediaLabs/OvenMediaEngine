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

class ID3v2TextFrame : public ID3v2Frame
{
public:
	ID3v2TextFrame(const ov::String &id, const ov::String &text)
	{
		SetFrameId(id);
		SetText(text);
	}

	virtual ~ID3v2TextFrame(){}
	
	void SetText(const ov::String &text)
	{
		_text = text;
	}
	const ov::String &GetText() const
	{
		return _text;
	}

	std::shared_ptr<ov::Data> GetData() const override
	{
		ov::ByteStream stream(_text.GetLength() + 1);

		stream.WriteBE(_encoding);
		stream.WriteText(_text);

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
	ov::String _text;
};