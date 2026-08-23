//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Keukhan
//  Copyright (c) 2024 OvenMedia Labs. All rights reserved.
//
//==============================================================================
#include "sei_event.h"

#define OV_LOG_TAG "SEIEvent"

bool SEIEvent::IsValid(const Json::Value &json)
{
	// asString() throws on a non string, which would surface as a 500 instead of a 400
	if ((json.isMember("seiType") == false) || (json["seiType"].isString() == false))
	{
		return false;
	}

	ov::String sei_type = json["seiType"].asString().c_str();
	if (sei_type.IsEmpty())
	{
		return false;
	}

	if (H264SEI::StringToPayloadType(sei_type) != H264SEI::PayloadType::USER_DATA_UNREGISTERED)
	{
		return false;
	}

	return true;
}

std::shared_ptr<SEIEvent> SEIEvent::Parse(const Json::Value &json)
{
	H264SEI::PayloadType payload_type = H264SEI::PayloadType::USER_DATA_UNREGISTERED;
	if (json.isMember("seiType") == true && json["seiType"].isString() == true)
	{
		payload_type = H264SEI::StringToPayloadType(ov::String(json["seiType"].asString().c_str()));
	}

	// Optional
	ov::String payload_data;
	if (json.isMember("data") == true && json["data"].isString() == true)
	{
		payload_data = json["data"].asString().c_str();
	}

	auto sei_event = std::make_shared<SEIEvent>();
	sei_event->SetSeiType(H264SEI::PayloadTypeToString(payload_type));
	sei_event->SetData(payload_data);
	// An API event is aimed at the next picture, so there is nothing to wait for
	sei_event->SetKeyframeOnly(false);

	return sei_event;
}

bool SEIEvent::IsValid(const pugi::xml_node &xml)
{
	ov::String sei_type = xml.child_value("SeiType");
	if (sei_type.IsEmpty())
	{
		sei_type = "UserDataUnregistered";
	}

	auto payload_type = H264SEI::StringToPayloadType(sei_type);
	if ((payload_type != H264SEI::PayloadType::USER_DATA_UNREGISTERED) &&
		(payload_type != H264SEI::PayloadType::PICTURE_TIMING))
	{
		return false;
	}

	return true;
}

std::shared_ptr<SEIEvent> SEIEvent::Parse(const pugi::xml_node &xml)
{
	ov::String sei_type = xml.child_value("SeiType");
	if (sei_type.IsEmpty())
	{
		sei_type = "UserDataUnregistered";
	}

	// Both optional: child_value() gives "" when the element is absent
	ov::String payload_data = xml.child_value("Data");
	bool keyframe_only		= (::strcmp(xml.child_value("KeyframeOnly"), "true") == 0);

	auto sei_event = std::make_shared<SEIEvent>();
	sei_event->SetSeiType(sei_type);
	sei_event->SetData(payload_data);
	sei_event->SetKeyframeOnly(keyframe_only);

	return sei_event;
}

H264SEI::PayloadType SEIEvent::GetSeiPayloadType() const
{
	return H264SEI::StringToPayloadType(_sei_type);
}

std::shared_ptr<ov::Data> SEIEvent::Serialize() const
{
	// An empty SeiType keeps the original default, user_data_unregistered
	const H264SEI::PayloadType payload_type =
		_sei_type.IsEmpty() ? H264SEI::PayloadType::USER_DATA_UNREGISTERED : GetSeiPayloadType();

	Json::Value json;
	json["seiType"]		 = H264SEI::PayloadTypeToString(payload_type).CStr();
	json["data"]		 = _data.CStr();
	json["keyframeOnly"] = _keyframe_only;

	// ${EpochTime} is left as it is: H264SeiInserter substitutes it at insertion time, so the
	// value is when the message entered the stream rather than when the event was queued.
	return ov::Json::Stringify(json).ToData(false);
}

std::shared_ptr<SEIEvent> SEIEvent::Deserialize(const std::shared_ptr<const ov::Data> &data)
{
	if ((data == nullptr) || (data->GetLength() == 0))
	{
		return nullptr;
	}

	auto json = ov::Json::Parse(data).GetJsonValue();
	if (json.isObject() == false)
	{
		logte("Could not parse SEI event (%zu bytes)", data->GetLength());
		return nullptr;
	}

	auto sei_event = std::make_shared<SEIEvent>();

	if (json.isMember("seiType") && json["seiType"].isString())
	{
		sei_event->_sei_type = json["seiType"].asString().c_str();
	}

	if (json.isMember("data") && json["data"].isString())
	{
		sei_event->_data = json["data"].asString().c_str();
	}

	if (json.isMember("keyframeOnly") && json["keyframeOnly"].isBool())
	{
		sei_event->_keyframe_only = json["keyframeOnly"].asBool();
	}

	return sei_event;
}
