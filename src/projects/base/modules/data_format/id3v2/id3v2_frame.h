//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2022 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <base/ovlibrary/ovlibrary.h>

class ID3v2Frame
{
public:
	ID3v2Frame();
	virtual ~ID3v2Frame();

	void SetFrameId(const ov::String &frame_id);

	// flags
	void SetTagAlterPreservation(bool tag_alter_preservation);
	void SetFileAlterPreservation(bool file_alter_preservation);
	void SetReadOnly(bool read_only);
	void SetGroupingIdentity(bool grouping_identity);
	void SetCompression(bool compression);
	void SetEncryption(bool encryption);
	void SetUnsynchronisation(bool unsynchronisation);
	void SetDataLengthIndicator(bool data_length_indicator);

	// Getters
	const ov::String &GetFrameId() const;
	bool IsTagAlterPreservation() const;
	bool IsFileAlterPreservation() const;
	bool IsReadOnly() const;
	bool IsGroupingIdentity() const;
	bool IsCompression() const;
	bool IsEncryption() const;
	bool IsUnsynchronisation() const;
	bool IsDataLengthIndicator() const;
	
	// Child class must override this function
	virtual std::shared_ptr<ov::Data> GetData() const = 0;

	std::shared_ptr<ov::Data> Serialize() const;

private:
	ov::String _frame_id; // 4 charaters capital A-Z and 0-9 

	// flags
	uint8_t	_flag_status_message = 0;
	uint8_t _flag_format_description = 0;
};