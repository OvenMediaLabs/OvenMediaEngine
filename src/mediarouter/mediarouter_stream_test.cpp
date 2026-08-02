//==============================================================================
//
//  OvenMediaEngine - Unit Tests
//
//  Covers: MediaRouteStream mirror buffer policies - RetainMirrorBuffer
//  (per-track keyframe-led retention) and BuildPastData (tap backfill assembly)
//
//==============================================================================
#include <gtest/gtest.h>

#include "mediarouter_stream.h"

namespace
{
	constexpr int64_t kRetentionUs = MEDIA_ROUTE_STREAM_MIRROR_RETENTION_MS * 1000;
	constexpr int64_t kBaseBackfillUs = MEDIA_ROUTE_STREAM_MIRROR_BASE_BACKFILL_MS * 1000;

	std::shared_ptr<MediaPacket> MakePacket(cmn::MediaType media_type, uint32_t track_id, bool keyframe)
	{
		return std::make_shared<MediaPacket>(media_type, track_id, nullptr, 0, 0, 0,
											 keyframe ? MediaPacketFlag::Key : MediaPacketFlag::NoFlag,
											 cmn::BitstreamFormat::Unknown, cmn::PacketType::Unknown);
	}

	std::shared_ptr<MediaRouteStream::MirrorBufferItem> MakeItem(cmn::MediaType media_type, uint32_t track_id, bool keyframe, int64_t dts_us = 0, int64_t age_ms = 0)
	{
		auto packet = MakePacket(media_type, track_id, keyframe);
		auto item = std::make_shared<MediaRouteStream::MirrorBufferItem>(packet, dts_us);
		item->created_time = std::chrono::steady_clock::now() - std::chrono::milliseconds(age_ms);
		return item;
	}
}  // namespace

TEST(MediaRouterRetainMirrorBuffer, KeyframeSupersedesTrackGop)
{
	MediaRouteStream::MirrorBufferMap buffers;
	buffers[1] = {MakeItem(cmn::MediaType::Video, 1, true), MakeItem(cmn::MediaType::Video, 1, false)};
	buffers[3] = {MakeItem(cmn::MediaType::Audio, 3, false)};
	auto audio = buffers[3][0];

	auto keyframe = MakePacket(cmn::MediaType::Video, 1, true);
	MediaRouteStream::RetainMirrorBuffer(buffers, keyframe, 0);

	// Track 1 holds only the new keyframe; the audio track is untouched
	ASSERT_EQ(buffers[1].size(), 1u);
	EXPECT_EQ(buffers[1][0]->packet, keyframe);
	ASSERT_EQ(buffers[3].size(), 1u);
	EXPECT_EQ(buffers[3][0], audio);
}

TEST(MediaRouterRetainMirrorBuffer, VideoStaysEmptyUntilKeyframe)
{
	MediaRouteStream::MirrorBufferMap buffers;

	auto non_key = MakePacket(cmn::MediaType::Video, 1, false);
	MediaRouteStream::RetainMirrorBuffer(buffers, non_key, 0);

	EXPECT_TRUE(buffers[1].empty());

	auto keyframe = MakePacket(cmn::MediaType::Video, 1, true);
	MediaRouteStream::RetainMirrorBuffer(buffers, keyframe, 0);

	ASSERT_EQ(buffers[1].size(), 1u);
	EXPECT_EQ(buffers[1][0]->packet, keyframe);
}

TEST(MediaRouterRetainMirrorBuffer, OverlongGopDiscardedWhole)
{
	// The GOP has grown past the retention window in media time, so the arriving
	// non-key packet discards the whole GOP and the track waits for a keyframe
	MediaRouteStream::MirrorBufferMap buffers;
	buffers[1] = {
		MakeItem(cmn::MediaType::Video, 1, true, 0),
		MakeItem(cmn::MediaType::Video, 1, false, 33000),
	};

	auto non_key = MakePacket(cmn::MediaType::Video, 1, false);
	MediaRouteStream::RetainMirrorBuffer(buffers, non_key, kRetentionUs + 1000);

	EXPECT_TRUE(buffers[1].empty());
}

TEST(MediaRouterRetainMirrorBuffer, NonVideoKeepsRetentionWindowOfContent)
{
	MediaRouteStream::MirrorBufferMap buffers;
	buffers[3] = {
		MakeItem(cmn::MediaType::Audio, 3, false, 0),	  // falls out of the window
		MakeItem(cmn::MediaType::Audio, 3, false, 2000000),
	};
	auto recent = buffers[3][1];

	auto packet = MakePacket(cmn::MediaType::Audio, 3, false);
	MediaRouteStream::RetainMirrorBuffer(buffers, packet, kRetentionUs + 1000000);

	ASSERT_EQ(buffers[3].size(), 2u);
	EXPECT_EQ(buffers[3][0], recent);
	EXPECT_EQ(buffers[3][1]->packet, packet);
}

TEST(MediaRouterBuildPastData, NonVideoCutAtEarliestKeyframeDts)
{
	MediaRouteStream::MirrorBufferMap buffers;
	buffers[1] = {
		MakeItem(cmn::MediaType::Video, 1, true, 5000000),
		MakeItem(cmn::MediaType::Video, 1, false, 5033000),
	};
	buffers[3] = {
		MakeItem(cmn::MediaType::Audio, 3, false, 4900000),	 // before the keyframe DTS: cut
		MakeItem(cmn::MediaType::Audio, 3, false, 5100000),
		MakeItem(cmn::MediaType::Audio, 3, false, 9000000),
	};

	auto past_data = MediaRouteStream::BuildPastData(buffers);

	ASSERT_EQ(past_data.size(), 4u);
	EXPECT_EQ(past_data[0], buffers[1][0]);
	EXPECT_EQ(past_data[1], buffers[1][1]);
	EXPECT_EQ(past_data[2], buffers[3][1]);
	EXPECT_EQ(past_data[3], buffers[3][2]);
}

TEST(MediaRouterBuildPastData, NonVideoBaseWindowWithoutVideo)
{
	// Without video the track keeps only the last base window of content,
	// anchored at its newest DTS
	MediaRouteStream::MirrorBufferMap buffers;
	buffers[3] = {
		MakeItem(cmn::MediaType::Audio, 3, false, 5000000 - kBaseBackfillUs - 1000),  // outside the window: cut
		MakeItem(cmn::MediaType::Audio, 3, false, 5000000 - kBaseBackfillUs + 1000),
		MakeItem(cmn::MediaType::Audio, 3, false, 5000000),
	};

	auto past_data = MediaRouteStream::BuildPastData(buffers);

	ASSERT_EQ(past_data.size(), 2u);
	EXPECT_EQ(past_data[0], buffers[3][1]);
	EXPECT_EQ(past_data[1], buffers[3][2]);
}

TEST(MediaRouterBuildPastData, StaleVideoTrackSkipped)
{
	// A stalled track still holding an old GOP contributes no past data; the
	// audio then falls back to its own base window
	MediaRouteStream::MirrorBufferMap buffers;
	buffers[1] = {
		MakeItem(cmn::MediaType::Video, 1, true, 0, MEDIA_ROUTE_STREAM_MIRROR_RETENTION_MS + 1000),
		MakeItem(cmn::MediaType::Video, 1, false, 33000, MEDIA_ROUTE_STREAM_MIRROR_RETENTION_MS + 500),
	};
	buffers[3] = {MakeItem(cmn::MediaType::Audio, 3, false, 1500000)};

	auto past_data = MediaRouteStream::BuildPastData(buffers);

	ASSERT_EQ(past_data.size(), 1u);
	EXPECT_EQ(past_data[0], buffers[3][0]);
}

TEST(MediaRouterBuildPastData, InterleavesTracksByDts)
{
	MediaRouteStream::MirrorBufferMap buffers;
	buffers[1] = {
		MakeItem(cmn::MediaType::Video, 1, true, 1000000),
		MakeItem(cmn::MediaType::Video, 1, false, 1066000),
	};
	buffers[3] = {
		MakeItem(cmn::MediaType::Audio, 3, false, 1030000),
		MakeItem(cmn::MediaType::Audio, 3, false, 1090000),
	};

	auto past_data = MediaRouteStream::BuildPastData(buffers);

	ASSERT_EQ(past_data.size(), 4u);
	EXPECT_EQ(past_data[0], buffers[1][0]);	 // video 1000000
	EXPECT_EQ(past_data[1], buffers[3][0]);	 // audio 1030000
	EXPECT_EQ(past_data[2], buffers[1][1]);	 // video 1066000
	EXPECT_EQ(past_data[3], buffers[3][1]);	 // audio 1090000
}

TEST(MediaRouterBuildPastData, EmptyBuffersReturnEmpty)
{
	MediaRouteStream::MirrorBufferMap buffers;

	auto past_data = MediaRouteStream::BuildPastData(buffers);

	EXPECT_TRUE(past_data.empty());
}
