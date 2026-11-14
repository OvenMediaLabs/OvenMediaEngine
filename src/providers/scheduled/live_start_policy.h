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
			std::vector<std::shared_ptr<MediaPacket>> replay;

			// Source time from the first replayed packet to the newest one
			int64_t replay_span_us = 0;
		};

		static std::shared_ptr<LiveStartPolicy> Create(Schedule::LiveStartPoint point);

		virtual ~LiveStartPolicy() = default;

		// Whether the source's frames since its latest keyframe are requested
		virtual bool NeedsPastData() const = 0;

		// Whether packets wait for their time on the channel clock
		virtual bool PacesToChannelClock() const = 0;

		// past_data: the requested frames, oldest first, on the timebases of source
		virtual StartPlan Plan(const std::vector<std::shared_ptr<MediaPacket>> &past_data,
							   const std::shared_ptr<info::Stream> &source) const = 0;
	};
}  // namespace pvd
