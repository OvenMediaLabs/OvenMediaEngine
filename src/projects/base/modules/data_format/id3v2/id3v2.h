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

#include "id3v2_frame.h"

constexpr uint8_t ID3V2_FIXED_HEADER_LENGTH = 10;

class ID3v2
{
public:
	ID3v2();
	~ID3v2();

	void SetVersion(uint8_t major, uint8_t revision);
	void SetUnsynchronisation(bool unsynchronisation);
	void SetExperimental(bool experimental);
	
	// The Extended Header and the Footer are not supported yet
	void SetExtendedHeader(bool extended_header);
	void SetFooterPresent(bool footer);

	void AddFrame(const std::shared_ptr<ID3v2Frame> &frame);

	uint8_t GetVersionMajor() const;
	uint8_t GetVersionRevision() const;
	bool IsUnsynchronisation() const;
	bool IsExtendedHeader() const;
	bool IsExperimental() const;
	bool IsFooterPresent() const;
	uint32_t GetSize() const;

	// Serialize
	std::shared_ptr<ov::Data> Serialize() const;

private:
	uint8_t _version_major = 4;
	uint8_t _version_minor = 0;
	uint8_t _flags = 0;
	uint32_t _size = 0;

	std::vector<std::shared_ptr<ID3v2Frame>> _frames;
	std::shared_ptr<ov::Data> _frame_data;

	// Extended Header is not supported yet
};