//==============================================================================
//
//  LiveStartPolicy
//
//  Created by Getroot
//  Copyright (c) 2026 OvenMediaLabs. All rights reserved.
//
//==============================================================================
#include "live_start_policy.h"

#include <base/ovlibrary/converter.h>

#include <algorithm>

namespace pvd
{
	namespace
	{
		int64_t DtsUs(const std::shared_ptr<MediaPacket> &packet, const std::shared_ptr<info::Stream> &source)
		{
			auto track = source->GetTrack(packet->GetTrackId());
			if (track == nullptr)
			{
				return 0;
			}

			auto timebase = track->GetTimeBase();
			return ov::Converter::Rescale(packet->GetDts() * timebase.GetNum(), 1000000, timebase.GetDen());
		}

		// Starts from the source's latest keyframe, so the item runs behind its source by the
		// source time buffered since that keyframe
		class LatestKeyframePolicy : public LiveStartPolicy
		{
		public:
			bool NeedsPastData() const override
			{
				return true;
			}

			StartPlan Plan(const std::vector<MediaRouterStreamTap::TappedPacket> &past_data,
						   const std::shared_ptr<info::Stream> &source) const override
			{
				StartPlan plan;

				auto keyframe_it = std::find_if(past_data.begin(), past_data.end(), [](const MediaRouterStreamTap::TappedPacket &tapped) {
					return tapped.packet->GetMediaType() == cmn::MediaType::Video;
				});

				// Nothing is decodable without a keyframe; start with the packets that arrive from now on
				if (keyframe_it == past_data.end() || keyframe_it->packet->IsKeyFrame() == false)
				{
					return plan;
				}

				plan.starts_at_keyframe = true;

				int64_t keyframe_us		  = DtsUs(keyframe_it->packet, source);
				int64_t release_offset_us = 0;

				// The buffered frames are the source's past, so they go out by their timestamps, not by how they
				// arrived; a stall or a burst of the source before the switch does not show. Kept in the router's
				// timestamp order, so the item's timeline starts at the keyframe
				for (const auto &tapped : past_data)
				{
					const auto &packet = tapped.packet;
					int64_t dts_us	   = DtsUs(packet, source);

					// Other tracks start with the keyframe
					if (packet->GetMediaType() != cmn::MediaType::Video && dts_us < keyframe_us)
					{
						continue;
					}

					// Data can be stamped ahead, such as an event with a start offset, so it goes out with the packet before it
					if (packet->GetMediaType() == cmn::MediaType::Video || packet->GetMediaType() == cmn::MediaType::Audio)
					{
						release_offset_us	= std::max(release_offset_us, dts_us - keyframe_us);
						plan.replay_span_us = release_offset_us;
					}

					plan.replay.push_back(ReplayPacket{packet, release_offset_us});
				}

				return plan;
			}
		};

		// Starts with the frame arriving at the switch, as received
		class NextKeyframePolicy : public LiveStartPolicy
		{
		public:
			bool NeedsPastData() const override
			{
				return false;
			}

			StartPlan Plan(const std::vector<MediaRouterStreamTap::TappedPacket> &,
						   const std::shared_ptr<info::Stream> &) const override
			{
				return {};
			}
		};
	}  // namespace

	std::shared_ptr<LiveStartPolicy> LiveStartPolicy::Create(Schedule::LiveStartPoint point)
	{
		switch (point)
		{
			case Schedule::LiveStartPoint::LatestKeyframe:
				return std::make_shared<LatestKeyframePolicy>();

			case Schedule::LiveStartPoint::NextKeyframe:
				return std::make_shared<NextKeyframePolicy>();
		}

		return nullptr;
	}
}  // namespace pvd
