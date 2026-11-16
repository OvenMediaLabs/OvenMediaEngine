//==============================================================================
//
//  LiveStartPolicy
//
//  Created by Getroot
//  Copyright (c) 2026 OvenMediaLabs. All rights reserved.
//
//==============================================================================
#pragma once

#include <base/info/stream.h>
#include <base/mediarouter/media_buffer.h>
#include <mediarouter/mediarouter_stream_tap.h>

#include "schedule.h"

namespace pvd
{
	// How a live item starts against its source. The playback loop passes facts
	// and carries out the answers.
	class LiveStartPolicy
	{
	public:
		struct StartPlan
		{
			// The item starts at a keyframe; false when no keyframe was buffered
			bool starts_at_keyframe = false;

			// Played first, in order, before the packets that arrive from now on
			std::vector<MediaRouterStreamTap::TappedPacket> replay;

			// When the keyframe reached the server. The playback loop holds every packet of the
			// item as long after its arrival as the keyframe has waited when the item starts
			std::chrono::steady_clock::time_point keyframe_received_time;
		};

		static std::shared_ptr<LiveStartPolicy> Create(Schedule::LiveStartPoint point);

		virtual ~LiveStartPolicy() = default;

		// Whether the source's frames since its latest keyframe are requested
		virtual bool NeedsPastData() const = 0;

		// past_data: the requested frames, oldest first, on the timebases of source
		virtual StartPlan Plan(const std::vector<MediaRouterStreamTap::TappedPacket> &past_data,
							   const std::shared_ptr<info::Stream> &source) const = 0;
	};
}  // namespace pvd
