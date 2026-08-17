//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2024 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <base/ovlibrary/ovlibrary.h>

/*
Data Structure

CueType : 8 bits
Duration - 32 bits / milliseconds, only positive value
Elapsed - 32 bits / milliseconds
Provisional - 8 bits, appended only when set (older payloads omit it)

*/

class CueEvent
{
public:
	enum class CueType : uint8_t
	{
		Unknown = 0,
		OUT,
		CONT,
		IN
	};

	static std::shared_ptr<CueEvent> Create(CueType cue_type, uint32_t duration_msec = 0, uint32_t elapsed_msec = 0, bool provisional = false);
	static std::shared_ptr<CueEvent> Parse(const std::shared_ptr<const ov::Data> &data);
	static CueType GetCueTypeByName(ov::String type);

	CueEvent(CueType cue_type, uint32_t duration_sec, uint32_t elapsed_msec, bool provisional);
	~CueEvent() = default;

	std::shared_ptr<ov::Data> Serialize() const;

	CueType GetCueType() const;
	ov::String GetCueTypeName() const;
	uint32_t GetDurationMsec() const;
	uint32_t GetElapsedMsec() const;
	// A provisional IN announces the planned return point and may still be
	// replaced by an explicit IN, earlier or later
	bool IsProvisional() const;

private:
	CueType _cue_type;
	uint32_t _duration_msec;
	uint32_t _elapsed_msec;
	bool _provisional = false;
};