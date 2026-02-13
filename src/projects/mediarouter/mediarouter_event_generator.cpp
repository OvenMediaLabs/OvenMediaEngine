//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Keukhan
//  Copyright (c) 2025 AirenSoft. All rights reserved.
//
//==============================================================================

#include "mediarouter_event_generator.h"

#include <base/ovlibrary/ovlibrary.h>
#include <modules/bitstream/h264/h264_sei.h>
#include <base/modules/data_format/amf_event/amf_event.h>
#include <orchestrator/orchestrator.h>

#include "mediarouter_private.h"

#ifdef OV_LOG_TAG
#	undef OV_LOG_TAG
#endif
#define OV_LOG_TAG "MediaRouter.Events"

/**
==============================================================================* 
	Configuration Examples
==============================================================================
<!-- Server.xml -->
<Application>
	<EventGenerator>
		<Enable>true</Enable>
		<Path>events/send_event_info.xml</Path>
	</EventGenerator>
</Application>
 */

MediaRouterEventGenerator::MediaRouterEventGenerator()
{
	_stop_watch.Start();
}

MediaRouterEventGenerator::~MediaRouterEventGenerator()
{
}

void MediaRouterEventGenerator::Init(const std::shared_ptr<info::Stream> &stream_info)
{
	
}

void MediaRouterEventGenerator::Update(
	const std::shared_ptr<info::Stream> &stream_info,
	const std::shared_ptr<MediaTrack> &media_track,
	const std::shared_ptr<MediaPacket> &media_packet)
{

}

const std::shared_ptr<pvd::Stream> MediaRouterEventGenerator::GetSourceStream(const std::shared_ptr<info::Stream> &stream_info)
{
	auto provider = std::dynamic_pointer_cast<pvd::Provider>(ocst::Orchestrator::GetInstance()->GetProviderFromType(stream_info->GetProviderType()));
	if (provider == nullptr)
	{
		logte("%s | Could not find a provider for scheme [%s]", stream_info->GetUri().CStr(), stream_info->GetProviderType());
		return nullptr;
	}

	auto application = provider->GetApplicationByName(stream_info->GetApplicationInfo().GetVHostAppName());
	if (application == nullptr)
	{
		logte("%s | Could not find an application for [%s]", stream_info->GetUri().CStr(), stream_info->GetApplicationInfo().GetVHostAppName().CStr());
		return nullptr;
	}

	return application->GetStreamByName(stream_info->GetName());
}

std::vector<std::shared_ptr<mr::EventInfo>> MediaRouterEventGenerator::ParseMatchedEvents(const std::shared_ptr<info::Stream> &stream_info)
{
	std::vector<std::shared_ptr<mr::EventInfo>> results;

	ov::String xml_real_path = _cfg_path;

	pugi::xml_document xml_doc;
	auto load_result = xml_doc.load_file(xml_real_path.CStr());
	if (load_result == false)
	{
		logte("%s | Failed to load file(%s) status(%d) description(%s)",
			  stream_info->GetUri().CStr(), xml_real_path.CStr(),
			  load_result.status, load_result.description());
		return {};
	}

	auto root_node = xml_doc.child("EventInfo");
	if (root_node.empty())
	{
		logte("%s | Failed to load Record info file(%s) because root node is not found",
			  stream_info->GetUri().CStr(), xml_real_path.CStr());
		return {};
	}

	for (pugi::xml_node curr_node = root_node.child("Event"); curr_node; curr_node = curr_node.next_sibling("Event"))
	{
		auto event = mr::EventInfo::Parse(stream_info, curr_node);
		if (event == nullptr)
		{
			continue;
		}

		logtw("%s | Matched Event. event(%s)", stream_info->GetUri().CStr(), event->GetInfoString().CStr());

		results.push_back(event);
	}

	return results;
}

void MediaRouterEventGenerator::MakeEvents(const std::shared_ptr<info::Stream> &stream_info)
{
	// Generate events
	for (auto &event : _events)
	{
		if (!(event->IsTrigger()))
			continue;

		logtt("%s | Event Triggered. event(%s)", stream_info->GetUri().CStr(), event->GetInfoString().CStr());
		
		if (!event->Update())
		{
			continue;
		}

		std::shared_ptr<ov::Data> event_data = event->Serialize();
		if(event_data == nullptr)
		{
			continue;
		}

		auto stream = GetSourceStream(stream_info);
		if (stream == nullptr)
		{
			logte("%s | Could not find a source stream.", stream_info->GetName().CStr());
			return;
		}

		if (!stream->SendDataFrame(
				-1,
				event->_event_format,
				event->_event_type,
				event_data,
				event->_urgent,
				true,
				event->_keyframe_only ? MediaPacketFlag::Key : MediaPacketFlag::NoFlag))
		{
			logte("%s | Failed to send event data", stream_info->GetName().CStr());
		}
	}
}
