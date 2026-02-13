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

#include <pugixml-1.9/src/pugixml.hpp>

/*
Example JSON format
-------------------
{
	"eventFormat": "sei",
	"eventType": "video",
	"urgent": true,
	"events": [
		{
			// <Must> 
			"seiType": "UserDataUnregistered",
			// <Must> 
			"data": "SEI Insertion Test - CurrentTime:${EpochTime}"
		}
	]
}
*/

/*
Example XML format
-------------------
<Event>
	<Enable>true</Enable>
	<SourceStreamName>stream</SourceStreamName>
	<Interval>3000</Interval>
	<EventFormat>sei</EventFormat>
	<!-- <EventType>video</EventType> -->
	<Values>
		<SeiType>UserDataUnregistered</SeiType>
		<Data>SEI Insertion Test - CurrentTime:${EpochTime}</Data>
		<KeyframeOnly>false</KeyframeOnly>
	</Values>
</Event>	
*/

class SEIEvent
{
public:
	static std::shared_ptr<SEIEvent> Create();

	static bool IsValid(const Json::Value &json);
	static std::shared_ptr<SEIEvent> Parse(const Json::Value &json);

	static bool IsValid(const pugi::xml_node &xml);
	static std::shared_ptr<SEIEvent> Parse(const pugi::xml_node &xml);

	SEIEvent();
	~SEIEvent() = default;

	std::shared_ptr<ov::Data> Serialize() const;

	void SetSeiType(ov::String sei_type)
	{
		_sei_type = sei_type;
	}

	void SetData(ov::String data)
	{
		_data = data;
	}

	void SetKeyframeOnly(bool keyframe_only)
	{
		_keyframe_only = keyframe_only;
	}

	bool IsKeyframeOnly() const
	{
		return _keyframe_only;
	}

private:
	ov::String _sei_type;
	ov::String _data;
	bool _keyframe_only = false;
};