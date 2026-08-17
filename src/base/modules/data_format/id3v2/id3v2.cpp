//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2022 AirenSoft. All rights reserved.
//
//==============================================================================
#include "id3v2.h"

ID3v2::ID3v2()
{

}

ID3v2::~ID3v2()
{

}

void ID3v2::SetVersion(uint8_t major, uint8_t revision)
{
	_version_major = major;
	_version_minor = revision;
}

void ID3v2::SetUnsynchronisation(bool unsynchronisation)
{
	_flags |= (unsynchronisation ? 0x80 : 0x00);
}

// The Extended Header and the Footer are not supported yet
void ID3v2::SetExtendedHeader(bool extended_header)
{
	_flags |= (extended_header ? 0x40 : 0x00);
}

void ID3v2::SetExperimental(bool experimental)
{
	_flags |= (experimental ? 0x20 : 0x00);
}

void ID3v2::SetFooterPresent(bool footer)
{
	_flags |= (footer ? 0x10 : 0x00);
}

void ID3v2::AddFrame(const std::shared_ptr<ID3v2Frame> &frame)
{
	_frames.push_back(frame);

	if (_frame_data == nullptr)
	{
		_frame_data = std::make_shared<ov::Data>(1024);
	}

	_frame_data->Append(frame->Serialize());
}

uint8_t ID3v2::GetVersionMajor() const
{
	return _version_major;
}

uint8_t ID3v2::GetVersionRevision() const
{
	return _version_minor;
}

bool ID3v2::IsUnsynchronisation() const
{
	return (_flags & 0x80) != 0;
}

bool ID3v2::IsExtendedHeader() const
{
	return (_flags & 0x40) != 0;
}

bool ID3v2::IsExperimental() const
{
	return (_flags & 0x20) != 0;
}

bool ID3v2::IsFooterPresent() const
{
	return (_flags & 0x10) != 0;
}

uint32_t ID3v2::GetSize() const
{
	// No extended header and no footer
	return _frame_data->GetLength();
}

// Serialize
std::shared_ptr<ov::Data> ID3v2::Serialize() const
{
	ov::ByteStream stream(4096);

	stream.WriteBE((uint8_t)0x49); // 'I'
	stream.WriteBE((uint8_t)0x44); // 'D'
	stream.WriteBE((uint8_t)0x33); // '3'
	stream.WriteBE(_version_major);
	stream.WriteBE(_version_minor);
	stream.WriteBE(_flags);
	stream.WriteBE32(ov::Converter::ToSynchSafe(GetSize()));
	stream.Write(_frame_data);
	
	return stream.GetDataPointer();
}