//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Keukhan
//  Copyright (c) 2025 AirenSoft. All rights reserved.
//
//==============================================================================
#include "event_forward.h"
#include "common.h"

namespace serdes
{
	/*
		{
		"dropEvent" : ["amf"]
		}
	*/
	std::shared_ptr<info::EventForward> EventForwardFromJson(const Json::Value &json_body)
	{
		auto event_forward = std::make_shared<info::EventForward>();

		// <Optional>
		auto json_drop_events = json_body["dropEvent"];
		if (json_drop_events.empty() == false && json_drop_events.isArray() == true)
		{
			for (uint32_t i = 0; i < json_drop_events.size(); i++)
			{
				if (json_drop_events[i].isString())
				{
					event_forward->AddDropEventString(json_drop_events[i].asString().c_str());
				}
			}
		}

		return event_forward;
	}
	Json::Value JsonFromEventForward(const std::shared_ptr<info::EventForward> &event_forward)
	{
		Json::Value response(Json::ValueType::objectValue);

		// Set dropEvent array
		Json::Value drop_event_array(Json::ValueType::arrayValue);
		for (const auto &event_string : event_forward->GetDropEventsString())
		{
			drop_event_array.append(event_string.CStr());
		}

		response["dropEvent"] = drop_event_array;

		return response;
	}

}  // namespace serdes