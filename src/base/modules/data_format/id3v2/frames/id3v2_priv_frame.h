//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2024 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include "../id3v2_frame.h"

class ID3v2PrivFrame : public ID3v2Frame
{
public:
	ID3v2PrivFrame(const ov::String &owner, const ov::String &private_data)
	{
		SetFrameId("PRIV");
		SetOwner(owner);
		SetPrivateData(private_data);
	}

	virtual ~ID3v2PrivFrame(){}
	
	void SetOwner(const ov::String &owner)
	{
		_owner = owner;
	}

	void SetPrivateData(const ov::String &data)
	{
		_private_data = data;
	}

	const ov::String &GetOwner() const
	{
		return _owner;
	}

	const ov::String &GetPrivateData() const
	{
		return _private_data;
	}

	std::shared_ptr<ov::Data> GetData() const override
	{
		// owner identifier + $00 + private data
		ov::ByteStream stream(_owner.GetLength() + 1 + _private_data.GetLength());

		stream.WriteText(_owner, true);
		stream.WriteText(_private_data, false);

		return stream.GetDataPointer();
	}

private:
	// <Header for 'Private frame', ID: "PRIV">
	// Owner identifier      <text string> $00
	// The private data      <binary data>
	ov::String _owner;
	ov::String _private_data;
};