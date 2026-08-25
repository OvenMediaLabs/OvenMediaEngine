//==============================================================================
//
//  OvenMediaEngine
//
//  Copyright (c) 2026 AirenSoft. All rights reserved.
//
//==============================================================================
#include <gtest/gtest.h>

#include "stream.h"

// The provider stream clock feeds cue markers and timed events. It is the
// newest dts over every media track, plus the wall time passed since it
// arrived.

namespace
{
	class ClockTestStream : public pvd::Stream
	{
	public:
		ClockTestStream()
			: pvd::Stream(StreamSourceType::Rtmp)
		{
		}

		void FeedPacket(uint32_t track_id, int64_t pts, int64_t dts, bool keyframe = false)
		{
			auto track = GetTrack(track_id);
			ASSERT_NE(track, nullptr);

			auto data = std::make_shared<ov::Data>();
			uint8_t byte = 0x00;
			data->Append(&byte, 1);

			auto packet = std::make_shared<MediaPacket>(track->GetMediaType(), track_id, data, pts, dts, 0,
														keyframe ? MediaPacketFlag::Key : MediaPacketFlag::NoFlag,
														cmn::BitstreamFormat::H264_ANNEXB, cmn::PacketType::NALU);
			UpdateLastTimestampStat(track, packet);
		}

		// The stored clock without the elapsed wall time, for exact assertions
		int64_t GetClockMs()
		{
			ov::LockGuard lock(_timestamp_mutex);
			return _last_media_timestamp_ms;
		}
	};

	std::shared_ptr<MediaTrack> MakeTrack(uint32_t id, cmn::MediaType type, int32_t timescale)
	{
		auto track = std::make_shared<MediaTrack>();
		track->SetId(id);
		track->SetMediaType(type);
		track->SetCodecId(type == cmn::MediaType::Video ? cmn::MediaCodecId::H264 : cmn::MediaCodecId::Aac);
		track->SetTimeBase(1, timescale);
		return track;
	}
}  // namespace

TEST(ProviderStreamClockTest, LeadingTrackDrivesTheClock)
{
	ClockTestStream stream;
	stream.AddTrack(MakeTrack(0, cmn::MediaType::Video, 90000));
	stream.AddTrack(MakeTrack(1, cmn::MediaType::Audio, 48000));

	stream.FeedPacket(0, 0, 0, true);
	stream.FeedPacket(1, 48 * 1500, 48 * 1500);	 // audio at 1500 ms, ahead of video

	EXPECT_EQ(stream.GetClockMs(), 1500);
	EXPECT_GE(stream.GetCurrentTimestampMs(), 1500);
}

TEST(ProviderStreamClockTest, LaggingTrackDoesNotPullTheClockBack)
{
	ClockTestStream stream;
	stream.AddTrack(MakeTrack(0, cmn::MediaType::Video, 90000));
	stream.AddTrack(MakeTrack(1, cmn::MediaType::Audio, 48000));

	stream.FeedPacket(1, 48 * 2000, 48 * 2000);
	stream.FeedPacket(0, 90 * 500, 90 * 500, true);	 // video still at 500 ms

	EXPECT_EQ(stream.GetClockMs(), 2000);
}

TEST(ProviderStreamClockTest, UnsetDtsFallsBackToPts)
{
	ClockTestStream stream;
	stream.AddTrack(MakeTrack(0, cmn::MediaType::Video, 90000));

	stream.FeedPacket(0, 90 * 700, -1, true);

	EXPECT_EQ(stream.GetClockMs(), 700);
}
