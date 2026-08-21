//==============================================================================
//
//  OvenMediaEngine
//
//  Copyright (c) 2026 AirenSoft. All rights reserved.
//
//==============================================================================
#include <gtest/gtest.h>

#include <base/modules/data_format/cue_event/cue_event.h>
#include <base/modules/data_format/scte35_event/scte35_event.h>

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

// A break that announces its own length gets its return point sent right after
// the break starts, so a packager never has to invent one.

namespace
{
	class EventCaptureStream : public pvd::Stream
	{
	public:
		EventCaptureStream()
			: pvd::Stream(StreamSourceType::Rtmp)
		{
			auto data_track = std::make_shared<MediaTrack>();
			data_track->SetId(9);
			data_track->SetMediaType(cmn::MediaType::Data);
			data_track->SetTimeBase(1, 1000);
			AddTrack(data_track);
		}

		bool SendFrame(const std::shared_ptr<MediaPacket> &packet) override
		{
			_sent.push_back(packet);
			return true;
		}

		std::vector<std::shared_ptr<MediaPacket>> _sent;
	};
}  // namespace

TEST(ProviderStreamBreakTest, CueBreakSendsItsReturnPoint)
{
	EventCaptureStream stream;

	auto out = CueEvent::Create(CueEvent::CueType::OUT, 12000);
	ASSERT_TRUE(stream.SendDataFrame(5000, cmn::BitstreamFormat::CUE, cmn::PacketType::EVENT, out->Serialize(), true));

	// The break and its return point
	ASSERT_EQ(stream._sent.size(), 2u);
	EXPECT_EQ(stream._sent[0]->GetDts(), 5000);

	auto returned = CueEvent::Parse(stream._sent[1]->GetData());
	ASSERT_NE(returned, nullptr);
	EXPECT_EQ(returned->GetCueType(), CueEvent::CueType::IN);
	EXPECT_TRUE(returned->IsProvisional());
	EXPECT_EQ(stream._sent[1]->GetDts(), 5000 + 12000);
}

TEST(ProviderStreamBreakTest, Scte35BreakFollowsItsAutoReturnFlag)
{
	{
		// auto return: the signal ends the break itself
		EventCaptureStream stream;
		auto out = Scte35Event::Create(mpegts::SpliceCommandType::SPLICE_INSERT, 1, true, 5000, 12000, true);
		ASSERT_TRUE(stream.SendDataFrame(5000, cmn::BitstreamFormat::SCTE35, cmn::PacketType::EVENT, out->Serialize(), true));

		ASSERT_EQ(stream._sent.size(), 2u);
		auto returned = Scte35Event::Parse(stream._sent[1]->GetData());
		ASSERT_NE(returned, nullptr);
		EXPECT_FALSE(returned->IsOutOfNetwork());
		EXPECT_TRUE(returned->IsProvisional());
		EXPECT_EQ(stream._sent[1]->GetDts(), 5000 + 12000);
	}

	{
		// no auto return: an explicit splice-in follows, so inventing one here
		// would collide with it
		EventCaptureStream stream;
		auto out = Scte35Event::Create(mpegts::SpliceCommandType::SPLICE_INSERT, 2, true, 5000, 12000, false);
		ASSERT_TRUE(stream.SendDataFrame(5000, cmn::BitstreamFormat::SCTE35, cmn::PacketType::EVENT, out->Serialize(), true));

		EXPECT_EQ(stream._sent.size(), 1u);
	}
}

TEST(ProviderStreamBreakTest, ReturnPointItselfSendsNothingFurther)
{
	EventCaptureStream stream;

	auto in = CueEvent::Create(CueEvent::CueType::IN, 0);
	ASSERT_TRUE(stream.SendDataFrame(5000, cmn::BitstreamFormat::CUE, cmn::PacketType::EVENT, in->Serialize(), true));

	EXPECT_EQ(stream._sent.size(), 1u);
}
