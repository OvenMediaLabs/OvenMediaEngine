//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2022 AirenSoft. All rights reserved.
//
//==============================================================================

#include "id3v2_frame.h"

ID3v2Frame::ID3v2Frame()
{

}

ID3v2Frame::~ID3v2Frame()
{

}


void ID3v2Frame::SetFrameId(const ov::String &frame_id)
{
	_frame_id = frame_id;
}

// flags
void ID3v2Frame::SetTagAlterPreservation(bool tag_alter_preservation)
{
	_flag_status_message |= (tag_alter_preservation ? 0x40 : 0x00);
}	

void ID3v2Frame::SetFileAlterPreservation(bool file_alter_preservation)
{
	_flag_status_message |= (file_alter_preservation ? 0x20 : 0x00);
}

void ID3v2Frame::SetReadOnly(bool read_only)
{
	_flag_status_message |= (read_only ? 0x10 : 0x00);
}

void ID3v2Frame::SetGroupingIdentity(bool grouping_identity)
{
	_flag_format_description |= (grouping_identity ? 0x40 : 0x00);
}

void ID3v2Frame::SetCompression(bool compression)
{
	_flag_format_description |= (compression ? 0x08 : 0x00);
}

void ID3v2Frame::SetEncryption(bool encryption)
{
	_flag_format_description |= (encryption ? 0x04 : 0x00);
}

void ID3v2Frame::SetUnsynchronisation(bool unsynchronisation)
{
	_flag_format_description |= (unsynchronisation ? 0x02 : 0x00);
}

void ID3v2Frame::SetDataLengthIndicator(bool data_length_indicator)
{
	_flag_format_description |= (data_length_indicator ? 0x01 : 0x00);
}

// Getters
const ov::String &ID3v2Frame::GetFrameId() const
{
	return _frame_id;
}

bool ID3v2Frame::IsTagAlterPreservation() const
{
	return (_flag_status_message & 0x40) != 0;
}

bool ID3v2Frame::IsFileAlterPreservation() const
{
	return (_flag_status_message & 0x20) != 0;
}

bool ID3v2Frame::IsReadOnly() const
{
	return (_flag_status_message & 0x10) != 0;
}

bool ID3v2Frame::IsGroupingIdentity() const
{
	return (_flag_format_description & 0x40) != 0;
}

bool ID3v2Frame::IsCompression() const
{
	return (_flag_format_description & 0x08) != 0;
}

bool ID3v2Frame::IsEncryption() const
{
	return (_flag_format_description & 0x04) != 0;
}

bool ID3v2Frame::IsUnsynchronisation() const
{
	return (_flag_format_description & 0x02) != 0;
}

bool ID3v2Frame::IsDataLengthIndicator() const
{
	return (_flag_format_description & 0x01) != 0;
}	

std::shared_ptr<ov::Data> ID3v2Frame::Serialize() const
{
	auto data = GetData();
	if (data == nullptr)
	{
		return nullptr;
	}

	ov::ByteStream stream(4096);

    //  Frame ID   $xx xx xx xx  (four characters)
    //  Size       4 * %0xxxxxxx
    //  Flags      $xx xx

	stream.Write(_frame_id.ToData(false));
	stream.WriteBE32(ov::Converter::ToSynchSafe(data->GetLength()));
	stream.WriteBE(_flag_status_message);
	stream.WriteBE(_flag_format_description);

	stream.Write(data);

	return stream.GetDataPointer();
}