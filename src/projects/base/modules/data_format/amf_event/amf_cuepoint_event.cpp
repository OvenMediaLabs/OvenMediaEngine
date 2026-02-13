//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Keukhan
//  Copyright (c) 2024 AirenSoft. All rights reserved.
//
//==============================================================================
#include "amf_cuepoint_event.h"

bool AmfCuePointEvent::IsMatch(const ov::String &amf_type)
{
	return amf_type.UpperCaseString() == ov::String("onCuePoint.Youtube").UpperCaseString() || 
		   amf_type.UpperCaseString() == ov::String("com.youtube.cuepoint").UpperCaseString();
}

std::shared_ptr<AmfCuePointEvent> AmfCuePointEvent::Create()
{
	return std::make_shared<AmfCuePointEvent>();
}

std::shared_ptr<AmfCuePointEvent> AmfCuePointEvent::Parse(const std::shared_ptr<ov::Data> &data)
{
	if (data == nullptr)
	{
		return nullptr;
	}

	auto object = std::make_shared<AmfCuePointEvent>();

	return object;
}

bool AmfCuePointEvent::IsValid(const Json::Value &json)
{
	if (json.isNull() || !json.isObject())
	{
		return false;
	}

	if (json.isMember("version") == false || json["version"].isString() == false)
	{
		return false;
	}

	if (json.isMember("preRollTimeSec") == false || json["preRollTimeSec"].isNumeric() == false)
	{
		return false;
	}

	return true;
}

std::shared_ptr<AmfCuePointEvent> AmfCuePointEvent::Parse(const Json::Value &json)
{
	if (json.isNull() || !json.isObject())
	{
		return nullptr;
	}

	auto object = std::make_shared<AmfCuePointEvent>();

	object->SetType("com.youtube.cuepoint");

	object->SetVersion(json["version"].asString().c_str());

	// [Must]
	if (json.isMember("preRollTimeSec") == true && json["preRollTimeSec"].isNumeric() == true)
	{
		object->SetPreRollTimeSec(json["preRollTimeSec"].asDouble());
	}
	else
	{
		object->SetPreRollTimeSec(0);
	}

	// [Optional]
	if (json.isMember("cuePointStart") == true && json["cuePointStart"].isBool() == true)
	{
		object->SetCuePointStart(json["cuePointStart"].asBool());
	}

	// [Must]
	if (json.isMember("breakDurationSec") == true && json["breakDurationSec"].isNumeric() == true)
	{
		object->SetBreakDurationSec(json["breakDurationSec"].asDouble());
	}
	else 
	{
		object->SetBreakDurationSec(0);
	}

	// [Optional]
	if (json.isMember("spliceEventId") == true && json["spliceEventId"].isNumeric() == true)
	{
		object->SetSpliceEventId(json["spliceEventId"].asDouble());
	}

	return object;
}

AmfCuePointEvent::AmfCuePointEvent()
{
	_objects = std::make_shared<AmfObject>();
}

void AmfCuePointEvent::SetType(const ov::String &type)
{
	_type = type;
}

void AmfCuePointEvent::SetVersion(const ov::String &version)
{
	_version = version;
}

void AmfCuePointEvent::SetPreRollTimeSec(const double &pre_roll_time_sec)
{
	_pre_roll_time_sec = pre_roll_time_sec;
}

void AmfCuePointEvent::SetCuePointStart(const bool &cue_point_start)
{
	_cue_point_start = cue_point_start;
}

void AmfCuePointEvent::SetBreakDurationSec(const double &break_duration_sec)
{
	_break_duration_sec = break_duration_sec;
}
void AmfCuePointEvent::SetSpliceEventId(const double &splice_event_id)
{
	_splice_event_id = splice_event_id;
}

std::shared_ptr<ov::Data> AmfCuePointEvent::Serialize() const
{
	AmfObject object;

	object.Append("type", AmfProperty(_type.CStr()));

	object.Append("version", AmfProperty(_version.CStr()));

	if(_pre_roll_time_sec != -1)
	{
		object.Append("pre_roll_time_sec", AmfProperty(_pre_roll_time_sec));
	}

	if(_cue_point_start == true)
	{
		object.Append("cue_point_start", AmfProperty(_cue_point_start));
	}

	if (_break_duration_sec != -1)
	{
		object.Append("break_duration_sec", AmfProperty(_break_duration_sec));
	}

	if (_splice_event_id != -1)
	{
		object.Append("splice_event_id", AmfProperty(_splice_event_id));
	}

	AmfDocument doc;
	doc.AppendProperty(AmfProperty("onCuePoint"));
	doc.AppendProperty(object);

	ov::ByteStream byte_stream;
	if (doc.Encode(byte_stream) == false)
	{
		return nullptr;
	}

	return byte_stream.GetDataPointer();
}