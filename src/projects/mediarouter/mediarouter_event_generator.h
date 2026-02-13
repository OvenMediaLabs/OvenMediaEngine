//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Keukhan
//  Copyright (c) 2025 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <base/provider/application.h>
#include <stdint.h>

#include <memory>
#include <queue>
#include <vector>

#include "base/info/stream.h"
#include "base/mediarouter/media_buffer.h"
#include "base/mediarouter/media_type.h"
#include "events/mediarouter_event_info.h"

class MediaRouterEventGenerator
{
public:
	MediaRouterEventGenerator();
	~MediaRouterEventGenerator();

	void Init(const std::shared_ptr<info::Stream> &stream_info);

	void Update(
		const std::shared_ptr<info::Stream> &stream_info,
		const std::shared_ptr<MediaTrack> &media_track,
		const std::shared_ptr<MediaPacket> &media_packet);

private:
	bool _cfg_enabled;
	ov::String _cfg_path;
	time_t _last_modified_time;

	static const std::shared_ptr<pvd::Stream> GetSourceStream(const std::shared_ptr<info::Stream> &stream_info);

	// Parsing event configuration XML
	std::vector<std::shared_ptr<mr::EventInfo>> ParseMatchedEvents(const std::shared_ptr<info::Stream> &stream_info);

	void MakeEvents(const std::shared_ptr<info::Stream> &stream_info);

	std::vector<std::shared_ptr<mr::EventInfo>> _events;
	ov::StopWatch _stop_watch;
};
