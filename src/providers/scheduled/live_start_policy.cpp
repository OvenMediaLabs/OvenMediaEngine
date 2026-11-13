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

		// Starts from the source's latest keyframe. The item is paced on the channel clock,
		// so it runs behind its source by the time since that keyframe; at channel start there
		// is nothing to continue from, so the buffered frames go out at once.
		class LatestKeyframePolicy : public LiveStartPolicy
		{
		public:
			bool NeedsPastData() const override
			{
				return true;
			}

			bool PacesToChannelClock() const override
			{
				return true;
			}

			StartPlan Plan(const std::vector<std::shared_ptr<MediaPacket>> &past_data,
						   const std::shared_ptr<info::Stream> &source,
						   bool channel_start) const override
			{
				StartPlan plan;

				auto keyframe_it = std::find_if(past_data.begin(), past_data.end(), [](const std::shared_ptr<MediaPacket> &packet) {
					return packet->GetMediaType() == cmn::MediaType::Video;
				});

				// Nothing is decodable without a keyframe; start with the packets that arrive from now on
				if (keyframe_it == past_data.end() || (*keyframe_it)->IsKeyFrame() == false)
				{
					return plan;
				}

				plan.starts_at_keyframe = true;

				int64_t keyframe_us = DtsUs(*keyframe_it, source);
				int64_t newest_us	= keyframe_us;

				for (const auto &packet : past_data)
				{
					int64_t dts_us = DtsUs(packet, source);

					// Other tracks start with the keyframe
					if (packet->GetMediaType() != cmn::MediaType::Video && dts_us < keyframe_us)
					{
						continue;
					}

					plan.replay.push_back(packet);
					newest_us = std::max(newest_us, dts_us);
				}

				plan.replay_span_us = newest_us - keyframe_us;

				if (channel_start)
				{
					plan.pacing_lead_us = plan.replay_span_us;
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

			bool PacesToChannelClock() const override
			{
				return false;
			}

			StartPlan Plan(const std::vector<std::shared_ptr<MediaPacket>> &,
						   const std::shared_ptr<info::Stream> &,
						   bool) const override
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
