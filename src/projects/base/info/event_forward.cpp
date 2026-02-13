//==============================================================================
//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Keukhan
//  Copyright (c) 2025 AirenSoft. All rights reserved.
//
//==============================================================================
#include "event_forward.h"

namespace info
{
	EventForward::EventForward()
	{
	}

	const std::vector<cmn::BitstreamFormat> &EventForward::GetDropEvents() const
	{
		return _drop_events;
	}

	void EventForward::AddDropEventString(ov::String event_string)
	{
		_drop_events_string.push_back(event_string);
	}

	const std::vector<ov::String> &EventForward::GetDropEventsString() const
	{
		return _drop_events_string;
	}

	bool EventForward::Validate() 
	{
		for(const auto &event_string : _drop_events_string)
		{
			bool found = false;
			for (const auto &available_event : _available_events)
			{
				if(event_string.UpperCaseString() == ov::String(cmn::GetBitstreamFormatString(available_event)).UpperCaseString())
				{
					_drop_events.push_back(available_event);
					found = true;
					break;
				}
			}

			if (!found)
			{
				return false;
			}
		}
		return true;
	}

	ov::String EventForward::ToString() const
	{
		ov::String result;
		for (const auto &event : _drop_events)
		{
			if (result.IsEmpty() == false)
			{
				result.Append(", ");
			}
			result.Append(cmn::GetBitstreamFormatString(event));
		}

		return result;
	}
}  // namespace info
