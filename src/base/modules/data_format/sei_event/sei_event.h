//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Kwon Keuk Han
//  Copyright (c) 2024 OvenMedia Labs. All rights reserved.
//
//==============================================================================
#pragma once

#include <base/ovlibrary/ovlibrary.h>
#include <modules/bitstream/h264/h264_sei.h>

#include <pugixml-1.9/src/pugixml.hpp>

/*
Request body of the sendEvent API. Only UserDataUnregistered is accepted here; PictureTiming is
configured in the EventGenerator XML because it writes into every picture rather than one.
-------------------------------------------------------
{
	"eventFormat": "sei",
	"eventType": "video",
	"urgent": true,
	"events": [
		{
			"seiType": "UserDataUnregistered",
			"data": "SEI Insertion Test - CurrentTime:${EpochTime}"
		}
	]
}
*/

/*
EventGenerator XML, SEI type 5 (user_data_unregistered)
-------------------------------------------------------
<Event>
	<Enable>true</Enable>
	<SourceStreamName>stream</SourceStreamName>
	<Interval>3000</Interval>
	<EventFormat>sei</EventFormat>
	<EventType>video</EventType>
	<Values>
		<SeiType>UserDataUnregistered</SeiType>
		<Data>SEI Insertion Test - CurrentTime:${EpochTime}</Data>
		<KeyframeOnly>false</KeyframeOnly>
	</Values>
</Event>
*/

/*
EventGenerator XML, SEI type 1 (pic_timing). No <Interval>: the event is queued once and then
stamps every picture, so there is nothing to repeat.
-------------------------------------------------------
<Event>
	<Enable>true</Enable>
	<SourceStreamName>stream</SourceStreamName>
	<EventFormat>sei</EventFormat>
	<Values>
		<SeiType>PictureTiming</SeiType>
		<!-- Optional. UTC (the default), Local, or a fixed offset: +09:00, -0500, +09 -->
		<Timezone>UTC</Timezone>
	</Values>
</Event>
*/

class SEIEvent
{
public:
	static bool IsValid(const Json::Value &json);
	static std::shared_ptr<SEIEvent> Parse(const Json::Value &json);

	static bool IsValid(const pugi::xml_node &xml);
	static std::shared_ptr<SEIEvent> Parse(const pugi::xml_node &xml);

	SEIEvent()	= default;
	~SEIEvent() = default;

	// {"seiType", "data", "keyframeOnly"} as JSON, carried on the event packet. EventConsumer is
	// the only reader: it builds the real SEI message from these fields, so these bytes never
	// reach an output.
	std::shared_ptr<ov::Data> Serialize() const;

	// Rebuilds the event from what Serialize() wrote; nullptr when the data is not that JSON, or
	// carries no seiType
	static std::shared_ptr<SEIEvent> Deserialize(const std::shared_ptr<const ov::Data> &data);

	// Which SEI message this event asks for
	H264SEI::PayloadType GetSeiPayloadType() const;

	void SetSeiType(ov::String sei_type)
	{
		_sei_type = sei_type;
	}

	void SetData(ov::String data)
	{
		_data = data;
	}

	// Kept verbatim, ${EpochTime} included: it is substituted when the message is inserted
	const ov::String &GetData() const
	{
		return _data;
	}

	void SetKeyframeOnly(bool keyframe_only)
	{
		_keyframe_only = keyframe_only;
	}

	bool IsKeyframeOnly() const
	{
		return _keyframe_only;
	}

	void SetTimezone(const H264SeiTimecodeZone &timezone)
	{
		_timezone = timezone;
	}

	// Which wall clock a pic_timing timecode counts from. Carried for every SEI type, but only
	// pic_timing has a time of day to read.
	const H264SeiTimecodeZone &GetTimezone() const
	{
		return _timezone;
	}

private:
	ov::String _sei_type;
	ov::String _data;
	bool _keyframe_only = false;
	H264SeiTimecodeZone _timezone;
};
