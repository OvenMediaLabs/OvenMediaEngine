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
		struct ReplayPacket
		{
			std::shared_ptr<MediaPacket> packet;

			// When it goes out, counted from the start of the item
			int64_t release_offset_us = 0;
		};

		struct StartPlan
		{
			// The item starts at a keyframe; false when no keyframe was buffered
			bool starts_at_keyframe = false;

			// Played first, in order, before the packets that arrive from now on
			std::vector<ReplayPacket> replay;

			// Source time from the keyframe to the newest replayed audio or video packet. The packets that
			// arrive from now on go out this long after they arrive, so they continue where the replay ends
			int64_t replay_span_us = 0;
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
