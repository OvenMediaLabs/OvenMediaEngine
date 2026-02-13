//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2024 AirenSoft. All rights reserved.
//
//==============================================================================
#include "cue_event.h"

std::shared_ptr<CueEvent> CueEvent::Create(CueType cue_type, uint32_t duration_sec, uint32_t elapsed_msec)
{
	return std::make_shared<CueEvent>(cue_type, duration_sec, elapsed_msec);
}

std::shared_ptr<CueEvent> CueEvent::Parse(const std::shared_ptr<const ov::Data> &data)
{
	if (data == nullptr)
	{
		return nullptr;
	}

	ov::ByteStream stream(data);

	if (data->GetLength() != 9)
	{
		return nullptr;
	}

	uint8_t cue_type_value = stream.Read8();
	if (cue_type_value > static_cast<uint8_t>(CueType::IN))
	{
		return nullptr;
	}

	CueType cue_type = static_cast<CueEvent::CueType>(cue_type_value);
	if (cue_type == CueType::Unknown)
	{
		return nullptr;
	}

	uint32_t duration_msec = stream.ReadBE32();
	uint32_t elapsed_msec = stream.ReadBE32();

	return Create(cue_type, duration_msec, elapsed_msec);
}

CueEvent::CueType CueEvent::GetCueTypeByName(ov::String type)
{
	if (type.UpperCaseString() == "OUT")
	{
		return CueType::OUT;
	}
	else if (type.UpperCaseString() == "CONT")
	{
		return CueType::CONT;
	}
	else if (type.UpperCaseString() == "IN")
	{
		return CueType::IN;
	}

	return CueType::Unknown;
}

CueEvent::CueEvent(CueType cue_type, uint32_t duration_sec, uint32_t elapsed_msec)
{
	_cue_type = cue_type;
	_duration_msec = duration_sec;
	_elapsed_msec = elapsed_msec;
}

std::shared_ptr<ov::Data> CueEvent::Serialize() const
{
	ov::ByteStream stream(5);

	stream.Write8(static_cast<uint8_t>(_cue_type));
	stream.WriteBE32(_duration_msec);
	stream.WriteBE32(_elapsed_msec);

	return stream.GetDataPointer();
}

CueEvent::CueType CueEvent::GetCueType() const
{
	return _cue_type;
}

ov::String CueEvent::GetCueTypeName() const
{
	switch (_cue_type)
	{
	case CueType::OUT:
		return "OUT";
	case CueType::CONT:
		return "OUT-CONT";
	case CueType::IN:
		return "IN";
	default:
		return "Unknown";
	}
}

uint32_t CueEvent::GetDurationMsec() const
{
	return _duration_msec;
}

uint32_t CueEvent::GetElapsedMsec() const
{
	return _elapsed_msec;
}