//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Kwon Keuk Han
//  Copyright (c) 2024 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <base/ovlibrary/ovlibrary.h>
#include <modules/rtmp/amf0/amf_document.h>

/*
	{
		"eventFormat": "amf",
		"events": [
			{
				"amfType": "onCuePoint.YouTube",    // Required
				"version": "0.1",					// Required
				"preRollTimeSec": 0,			    // Required
				"cuePointStart": false,				// Optional (default: false)
				"breakDurationSec": 30,				// Optional
				"spliceEventId": 0					// Optional
			}
		]
	}
*/

class AmfCuePointEvent
{
public:
	static bool IsMatch(const ov::String &amf_type);
	static std::shared_ptr<AmfCuePointEvent> Create();
	static std::shared_ptr<AmfCuePointEvent> Parse(const std::shared_ptr<ov::Data> &data);

	static bool IsValid(const Json::Value &json);
	static std::shared_ptr<AmfCuePointEvent> Parse(const Json::Value &json);

	AmfCuePointEvent();
	~AmfCuePointEvent() = default;

	void SetType(const ov::String &type);
	void SetVersion(const ov::String &version);
	void SetPreRollTimeSec(const double &pre_roll_time_sec);
	void SetCuePointStart(const bool &cue_point_start);
	void SetBreakDurationSec(const double &break_duration_sec);
	void SetSpliceEventId(const double &splice_event_id);

	std::shared_ptr<ov::Data> Serialize() const;

private:
	std::shared_ptr<AmfObject> _objects = nullptr;

	ov::String _type;
	ov::String _version;
	double _pre_roll_time_sec = -1;
	bool _cue_point_start = false;
	double _break_duration_sec = -1;
	double _splice_event_id = -1;
};