//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Keukhan
//  Copyright (c) 2024 AirenSoft. All rights reserved.
//
//==============================================================================
#include "sei_event.h"

#include <modules/bitstream/h264/h264_sei.h>

std::shared_ptr<SEIEvent> SEIEvent::Create()
{
	return std::make_shared<SEIEvent>();
}

SEIEvent::SEIEvent()
{
}

bool SEIEvent::IsValid(const Json::Value &json)
{
	// Check Payload Type
	ov::String sei_type = json["seiType"].asString().c_str();
	if (sei_type.IsEmpty())
	{
		sei_type = "UserDataUnregistered";
	}

	if (H264SEI::StringToPayloadType(sei_type) != H264SEI::PayloadType::USER_DATA_UNREGISTERED)
	{
		// Only 'UserDataUnregistered' type is supported.
		return false;
	}

	// Data (optional)
	// if (json["data"].isNull() || !json["data"].isObject())
	// {
	// 	return false;
	// }

	return true;
}

std::shared_ptr<SEIEvent> SEIEvent::Parse(const Json::Value &json)
{
	auto event						  = json;

	// Payload Type
	H264SEI::PayloadType payload_type = H264SEI::PayloadType::USER_DATA_UNREGISTERED;
	if (event.isMember("seiType") == true && event["seiType"].isString() == true)
	{
		payload_type = H264SEI::StringToPayloadType(ov::String(event["seiType"].asString().c_str()));
	}

	// Data (optional)
	ov::String payload_data;
	if (event.isMember("data") == true && event["data"].isString() == true)
	{
		payload_data = event["data"].asString().c_str();
	}

	auto sei_event = std::make_shared<SEIEvent>();
	sei_event->SetSeiType(H264SEI::PayloadTypeToString(payload_type));
	sei_event->SetData(payload_data);
	sei_event->SetKeyframeOnly(false);

	return sei_event;
}

bool SEIEvent::IsValid(const pugi::xml_node &xml)
{
	// Check Payload Type
	ov::String sei_type = xml.child_value("SeiType");
	if (sei_type.IsEmpty())
	{
		sei_type = "UserDataUnregistered";
	}
	if (H264SEI::StringToPayloadType(sei_type) != H264SEI::PayloadType::USER_DATA_UNREGISTERED)
	{
		// Only 'UserDataUnregistered' type is supported.
		return false;
	}

	// Data (optional)
	// auto data_node = xml.child("Data");
	// if (data_node.empty())
	// {
	// 	return false;
	// }

	return true;
}

std::shared_ptr<SEIEvent> SEIEvent::Parse(const pugi::xml_node &xml)
{
	// Payload Type
	ov::String sei_type = xml.child_value("SeiType");
	if (sei_type.IsEmpty())
	{
		sei_type = "UserDataUnregistered";
	}

	// Data (optional)
	ov::String payload_data = xml.child_value("Data");

	// KeyframeOnly (optional)
	bool keyframe_only	= xml.child("KeyframeOnly") ? (strcmp(xml.child_value("KeyframeOnly"), "true") == 0) : false;


	auto sei_event = std::make_shared<SEIEvent>();
	sei_event->SetSeiType(sei_type);
	sei_event->SetData(payload_data);
	sei_event->SetKeyframeOnly(keyframe_only);

	return sei_event;
}

std::shared_ptr<ov::Data> SEIEvent::Serialize() const
{
	H264SEI::PayloadType payload_type = H264SEI::PayloadType::USER_DATA_UNREGISTERED;
	if (!_sei_type.IsEmpty())
	{
		payload_type = H264SEI::StringToPayloadType(_sei_type);
	}
	// Data (optional)
	ov::String payload_data = _data.IsEmpty() ? "" : _data;

	// Replace ${EpochTime} with current epoch time
	auto current_time = ov::Time::GetTimestampInMs();
	payload_data = payload_data.Replace("${EpochTime}", ov::String::FormatString("%" PRId64, current_time));

	auto sei = std::make_shared<H264SEI>();
	sei->SetPayloadType(payload_type);
	sei->SetPayloadTimeCode(current_time);
	sei->SetPayloadData(payload_data.ToData());

	return sei->Serialize();
}