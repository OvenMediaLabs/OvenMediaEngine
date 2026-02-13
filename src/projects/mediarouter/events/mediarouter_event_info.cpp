//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Keukhan
//  Copyright (c) 2025 AirenSoft. All rights reserved.
//
//==============================================================================

#include "mediarouter_event_info.h"

#include <base/ovlibrary/ovlibrary.h>
#include <orchestrator/orchestrator.h>

#include "../mediarouter_private.h"

#ifdef OV_LOG_TAG
#	undef OV_LOG_TAG
#endif
#define OV_LOG_TAG "MediaRouter.Events"

/*
<!-- send_event_info.xml -->
<?xml version="1.0" encoding="UTF-8"?>
<EventInfo>
	//  Default format for all events
	<Event>
		<Enable>true</Enable>							// Optional (default: false)
		<SourceStreamName>*</SourceStreamName>			// Must
		<Interval>2000</Interval>						// Must
		<EventFormat>sei</EventFormat>					// Must
		<EventType>video</EventType>					// Optional (default: video)
		<Values>										// Must
			...											// * Refer to each event format
		</Values>
	</Event>
	...
</EventInfo>
*/

namespace mr
{
	std::shared_ptr<mr::EventInfo> EventInfo::Parse(const std::shared_ptr<info::Stream> &stream_info, const pugi::xml_node &event_node)
	{
		// Enable
		auto enable_string = event_node.child_value("Enable");
		if (enable_string != nullptr && strlen(enable_string) > 0)
		{
			if ((strcmp(enable_string, "true") == 0) == false)
			{
				return nullptr;
			}
		}

		// SourceStreamName
		ov::String source_stream_name = event_node.child_value("SourceStreamName");
		if (source_stream_name.IsEmpty())
		{
			// No Required SourceStreamName
			return nullptr;
		}
		ov::Regex _source_stream_name_regex = ov::Regex::CompiledRegex(ov::Regex::WildCardRegex(source_stream_name));
		auto match_result					= _source_stream_name_regex.Matches(stream_info->GetName().CStr());
		if (!match_result.IsMatched())
		{
			return nullptr;
		}

		// Urgent
		bool urgent		 = event_node.child("Urgent") ? (strcmp(event_node.child_value("Urgent"), "true") == 0) : false;

		// Interval
		int32_t interval = event_node.child("Interval") ? event_node.child("Interval").text().as_int() : 0;

		// Event Type
		cmn::PacketType event_type;
		ov::String event_type_string = event_node.child_value("EventType");
		if (event_type_string.IsEmpty())
		{
			event_type = cmn::PacketType::EVENT;
		}
		else
		{
			if (event_type_string.UpperCaseString() == "VIDEO")
			{
				event_type = cmn::PacketType::VIDEO_EVENT;
			}
			else if (event_type_string.UpperCaseString() == "AUDIO")
			{
				event_type = cmn::PacketType::AUDIO_EVENT;
			}
			else if (event_type_string.UpperCaseString() == "EVENT")
			{
				event_type = cmn::PacketType::EVENT;
			}
			else
			{
				// Not supported event type
				logtw("Not supported EventType: %s", event_type_string.CStr());

				return nullptr;
			}
		}

		// Event Format
		cmn::BitstreamFormat event_format = cmn::BitstreamFormat::Unknown;
		ov::String event_format_string	  = event_node.child_value("EventFormat");
		if (event_format_string.UpperCaseString() == "SEI")
		{
			event_format = cmn::BitstreamFormat::SEI;
		}
		else if (event_format_string.UpperCaseString() == "AMF")
		{
			event_format = cmn::BitstreamFormat::AMF;
		}
		else if (event_format_string.UpperCaseString() == "CUE")
		{
			event_format = cmn::BitstreamFormat::CUE;
		}
		else if (event_format_string.UpperCaseString() == "ID3" || event_format_string.UpperCaseString() == "ID3V2")
		{
			event_format = cmn::BitstreamFormat::ID3v2;
		}
		else
		{
			// Not supported event format
			logtw("Not supported EventFormat. EventFormat%s", event_format_string.CStr());
			return nullptr;
		}

		auto event_info = std::make_shared<mr::EventInfo>(event_format, event_type, urgent, interval);
		if (event_info == nullptr)
		{
			logtw("Could not allocate EventInfo.");
			return nullptr;
		}

		// Values
		if (!event_info->ParseValues(event_node.child("Values")))
		{
			std::ostringstream oss;
			event_node.print(oss, " ");
			logtw("Could not parse Values for EventInfo. XML:\n %s", oss.str().c_str());
			return nullptr;
		}

		return event_info;
	}

	bool EventInfo::ParseValues(const pugi::xml_node &values_node)
	{
		// Parse values based on event format
		switch (_event_format)
		{
			case cmn::BitstreamFormat::SEI: {
				if (SEIEvent::IsValid(values_node))
				{
					if (_sei_event = SEIEvent::Parse(values_node))
					{
						SetKeyframeOnly(_sei_event->IsKeyframeOnly());
						return true;
					}
				}
			}
			break;
			case cmn::BitstreamFormat::AMF: {
				if (AmfTextDataEvent::IsValid(values_node))
				{
					if (_amf_textdata_event = AmfTextDataEvent::Parse(values_node))
					{
						return true;
					}
				}
				else if (AmfUserDataEvent::IsValid(values_node))
				{
					if (_amf_userdata_event = AmfUserDataEvent::Parse(values_node))
					{
						return true;
					}
				}
			}
			break;
			case cmn::BitstreamFormat::CUE:
			case cmn::BitstreamFormat::ID3v2:
			default: {
				// Not supported event format
			}
			break;
		}

		return false;
	}

	std::shared_ptr<ov::Data> EventInfo::Serialize()
	{
		switch (_event_format)
		{
			case cmn::BitstreamFormat::SEI: {
				if (_sei_event != nullptr)
					return _sei_event->Serialize();
			}
			break;
			case cmn::BitstreamFormat::AMF: {
				if (_amf_textdata_event != nullptr)
					return _amf_textdata_event->Serialize();
				
				if (_amf_userdata_event != nullptr)
					return _amf_userdata_event->Serialize();
			}
			break;

			case cmn::BitstreamFormat::CUE:
			case cmn::BitstreamFormat::ID3v2:
			default:
				// Not supported event format
				break;
		}

		return nullptr;
	}
}  // namespace mr