//==============================================================================
//
//  OvenMediaEngine - Unit Tests
//
//  Covers: MediaRouteStream::SelectPastData - past-data range selection for
//  new stream taps (mirror buffer backfill)
//
//==============================================================================
#include <gtest/gtest.h>

#include "mediarouter_stream.h"

namespace
{
	std::shared_ptr<MediaRouteStream::MirrorBufferItem> MakeItem(cmn::MediaType media_type, uint32_t track_id, bool keyframe)
	{
		auto packet = std::make_shared<MediaPacket>(media_type, track_id, nullptr, 0, 0, 0,
													keyframe ? MediaPacketFlag::Key : MediaPacketFlag::NoFlag,
													cmn::BitstreamFormat::Unknown, cmn::PacketType::Unknown);
		return std::make_shared<MediaRouteStream::MirrorBufferItem>(packet);
	}
}  // namespace

TEST(MediaRouterSelectPastData, VideoStartsAtMostRecentKeyframe)
{
	std::vector<std::shared_ptr<MediaRouteStream::MirrorBufferItem>> buffer{
		MakeItem(cmn::MediaType::Video, 1, true),
		MakeItem(cmn::MediaType::Video, 1, false),
		MakeItem(cmn::MediaType::Video, 1, false),
		MakeItem(cmn::MediaType::Video, 1, true),
		MakeItem(cmn::MediaType::Video, 1, false),
		MakeItem(cmn::MediaType::Video, 1, false),
	};

	auto selected = MediaRouteStream::SelectPastData(buffer);

	ASSERT_EQ(selected.size(), 3u);
	EXPECT_EQ(selected[0], buffer[3]);
	EXPECT_EQ(selected[1], buffer[4]);
	EXPECT_EQ(selected[2], buffer[5]);
}

TEST(MediaRouterSelectPastData, NonVideoFollowsEarliestVideoStart)
{
	// Video track 1 keyframes at 1 and 6, video track 2 keyframe at 4, audio track 3 interleaved.
	// Expected: track 1 from 6, track 2 from 4, audio from min(6, 4) = 4.
	std::vector<std::shared_ptr<MediaRouteStream::MirrorBufferItem>> buffer{
		MakeItem(cmn::MediaType::Audio, 3, false),	// 0: dropped
		MakeItem(cmn::MediaType::Video, 1, true),	// 1: dropped (older keyframe)
		MakeItem(cmn::MediaType::Video, 2, false),	// 2: dropped
		MakeItem(cmn::MediaType::Audio, 3, false),	// 3: dropped
		MakeItem(cmn::MediaType::Video, 2, true),	// 4: kept
		MakeItem(cmn::MediaType::Audio, 3, false),	// 5: kept
		MakeItem(cmn::MediaType::Video, 1, true),	// 6: kept
		MakeItem(cmn::MediaType::Video, 2, false),	// 7: kept
		MakeItem(cmn::MediaType::Audio, 3, false),	// 8: kept
	};

	auto selected = MediaRouteStream::SelectPastData(buffer);

	ASSERT_EQ(selected.size(), 5u);
	EXPECT_EQ(selected[0], buffer[4]);
	EXPECT_EQ(selected[1], buffer[5]);
	EXPECT_EQ(selected[2], buffer[6]);
	EXPECT_EQ(selected[3], buffer[7]);
	EXPECT_EQ(selected[4], buffer[8]);
}

TEST(MediaRouterSelectPastData, VideoBeforeItsOwnKeyframeIsDroppedAfterCommonStart)
{
	// Track 2's packets between the common start and its own keyframe must be dropped
	std::vector<std::shared_ptr<MediaRouteStream::MirrorBufferItem>> buffer{
		MakeItem(cmn::MediaType::Video, 1, true),	// 0: kept (track 1 start)
		MakeItem(cmn::MediaType::Video, 2, false),	// 1: dropped (before track 2 keyframe)
		MakeItem(cmn::MediaType::Video, 2, true),	// 2: kept (track 2 start)
		MakeItem(cmn::MediaType::Video, 1, false),	// 3: kept
	};

	auto selected = MediaRouteStream::SelectPastData(buffer);

	ASSERT_EQ(selected.size(), 3u);
	EXPECT_EQ(selected[0], buffer[0]);
	EXPECT_EQ(selected[1], buffer[2]);
	EXPECT_EQ(selected[2], buffer[3]);
}

TEST(MediaRouterSelectPastData, KeepsFullBufferWhenVideoHasNoKeyframe)
{
	std::vector<std::shared_ptr<MediaRouteStream::MirrorBufferItem>> buffer{
		MakeItem(cmn::MediaType::Audio, 3, false),
		MakeItem(cmn::MediaType::Video, 1, false),
		MakeItem(cmn::MediaType::Audio, 3, false),
		MakeItem(cmn::MediaType::Video, 1, false),
	};

	auto selected = MediaRouteStream::SelectPastData(buffer);

	ASSERT_EQ(selected.size(), buffer.size());
	for (size_t i = 0; i < buffer.size(); i++)
	{
		EXPECT_EQ(selected[i], buffer[i]);
	}
}

TEST(MediaRouterSelectPastData, KeepsFullBufferWhenNoVideoTrack)
{
	std::vector<std::shared_ptr<MediaRouteStream::MirrorBufferItem>> buffer{
		MakeItem(cmn::MediaType::Audio, 3, false),
		MakeItem(cmn::MediaType::Audio, 3, false),
	};

	auto selected = MediaRouteStream::SelectPastData(buffer);

	ASSERT_EQ(selected.size(), buffer.size());
	EXPECT_EQ(selected[0], buffer[0]);
	EXPECT_EQ(selected[1], buffer[1]);
}

TEST(MediaRouterSelectPastData, EmptyBufferReturnsEmpty)
{
	std::vector<std::shared_ptr<MediaRouteStream::MirrorBufferItem>> buffer;

	auto selected = MediaRouteStream::SelectPastData(buffer);

	EXPECT_TRUE(selected.empty());
}

TEST(MediaRouterSelectPastData, MaxCountKeepsNewestAndVideoStaysKeyframeLed)
{
	// Selection is all 7 items (track 1 keyframe at 0, track 2 keyframe at 4);
	// cap 4 keeps the newest 4, track 2 survives keyframe-led, track 1 whose
	// keyframe fell before the cap is dropped entirely
	std::vector<std::shared_ptr<MediaRouteStream::MirrorBufferItem>> buffer{
		MakeItem(cmn::MediaType::Video, 1, true),	// 0: dropped by cap
		MakeItem(cmn::MediaType::Audio, 3, false),	// 1: dropped by cap
		MakeItem(cmn::MediaType::Video, 1, false),	// 2: dropped by cap
		MakeItem(cmn::MediaType::Audio, 3, false),	// 3: kept (audio is independent)
		MakeItem(cmn::MediaType::Video, 2, true),	// 4: kept (keyframe survives the trim)
		MakeItem(cmn::MediaType::Video, 2, false),	// 5: kept
		MakeItem(cmn::MediaType::Audio, 3, false),	// 6: kept
	};

	auto selected = MediaRouteStream::SelectPastData(buffer, 4);

	ASSERT_EQ(selected.size(), 4u);
	EXPECT_EQ(selected[0], buffer[3]);
	EXPECT_EQ(selected[1], buffer[4]);
	EXPECT_EQ(selected[2], buffer[5]);
	EXPECT_EQ(selected[3], buffer[6]);
}

TEST(MediaRouterSelectPastData, MaxCountDropsVideoTrackWithoutKeyframeInWindow)
{
	// After the cap trim track 1 has no keyframe left, so only audio survives
	std::vector<std::shared_ptr<MediaRouteStream::MirrorBufferItem>> buffer{
		MakeItem(cmn::MediaType::Video, 1, true),	// 0: dropped by cap
		MakeItem(cmn::MediaType::Video, 1, false),	// 1: dropped (no keyframe in window)
		MakeItem(cmn::MediaType::Audio, 3, false),	// 2: kept
		MakeItem(cmn::MediaType::Video, 1, false),	// 3: dropped (no keyframe in window)
		MakeItem(cmn::MediaType::Audio, 3, false),	// 4: kept
	};

	auto selected = MediaRouteStream::SelectPastData(buffer, 4);

	ASSERT_EQ(selected.size(), 2u);
	EXPECT_EQ(selected[0], buffer[2]);
	EXPECT_EQ(selected[1], buffer[4]);
}

TEST(MediaRouterSelectPastData, MaxCountNotExceededLeavesSelectionUnchanged)
{
	std::vector<std::shared_ptr<MediaRouteStream::MirrorBufferItem>> buffer{
		MakeItem(cmn::MediaType::Video, 1, true),
		MakeItem(cmn::MediaType::Video, 1, false),
		MakeItem(cmn::MediaType::Audio, 3, false),
	};

	auto selected = MediaRouteStream::SelectPastData(buffer, 10);

	ASSERT_EQ(selected.size(), 3u);
	EXPECT_EQ(selected[0], buffer[0]);
	EXPECT_EQ(selected[1], buffer[1]);
	EXPECT_EQ(selected[2], buffer[2]);
}
