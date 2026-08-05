//==============================================================================
//
//  OvenMediaEngine
//
//  Copyright (c) 2026 OvenMediaLabs. All rights reserved.
//
//==============================================================================
#include <gtest/gtest.h>

#include <modules/ffmpeg/compat.h>

#include "filter_fps.h"

// The FPS filter emits one frame per output slot. Pop() parks the last pushed
// frame until its successor arrives, so a filter replacement must Flush() the
// parked frame and hand the slot position over (SetContinuationPts) or one
// output slot is silently lost at every replacement.

namespace
{
	constexpr int64_t kSlotTicks = 3000;  // 1/90000 timebase, 30 fps

	FilterFps MakeFilter()
	{
		FilterFps filter;
		filter.SetInputTimebase(cmn::Timebase(1, 90000));
		filter.SetInputFrameRate(30.0);
		filter.SetOutputFrameRate(30.0);
		filter.SetSkipFrames(FilterFps::SkipFramesDisabled);
		filter.SetOutputFrameCopyMode(FilterFps::OutputFrameCopyMode::ShallowCopy);
		return filter;
	}

	std::shared_ptr<MediaFrame> MakeFrame(int64_t slot)
	{
		AVFrame *av_frame = ::av_frame_alloc();
		av_frame->format  = AV_PIX_FMT_YUV420P;
		av_frame->width	  = 64;
		av_frame->height  = 64;
		av_frame->pts	  = slot * kSlotTicks;

		if (::av_frame_get_buffer(av_frame, 0) < 0)
		{
			::av_frame_free(&av_frame);
			return nullptr;
		}

		auto media_frame = ffmpeg::compat::ToMediaFrame(cmn::MediaType::Video, av_frame);
		::av_frame_free(&av_frame);

		return media_frame;
	}

	std::vector<int64_t> DrainSlots(FilterFps &filter)
	{
		std::vector<int64_t> slots;
		while (auto frame = filter.Pop())
		{
			slots.push_back(frame->GetPts() / kSlotTicks);
		}
		return slots;
	}
}  // namespace

TEST(FilterFpsTest, CfrPassThrough)
{
	auto filter = MakeFilter();

	std::vector<int64_t> emitted;
	for (int64_t slot = 0; slot <= 5; slot++)
	{
		ASSERT_TRUE(filter.Push(MakeFrame(slot)));
		for (auto s : DrainSlots(filter))
		{
			emitted.push_back(s);
		}
	}

	// One frame per slot; the newest one stays parked awaiting its successor
	EXPECT_EQ(emitted, (std::vector<int64_t>{0, 1, 2, 3, 4}));
}

TEST(FilterFpsTest, FlushEmitsParkedFrame)
{
	auto filter = MakeFilter();

	ASSERT_TRUE(filter.Push(MakeFrame(0)));
	ASSERT_TRUE(filter.Push(MakeFrame(1)));
	EXPECT_EQ(DrainSlots(filter), (std::vector<int64_t>{0}));

	// The parked frame comes out at its own slot instead of being dropped
	auto flushed = filter.Flush();
	ASSERT_NE(flushed, nullptr);
	EXPECT_EQ(flushed->GetPts(), 1 * kSlotTicks);
	EXPECT_EQ(flushed->GetDuration(), kSlotTicks);

	EXPECT_EQ(filter.Flush(), nullptr);
}

TEST(FilterFpsTest, ContinuationFillsSwapHole)
{
	// The old filter ends after emitting slots 0..2 and flushing the parked 3
	auto old_filter = MakeFilter();
	std::vector<int64_t> emitted;
	for (int64_t slot = 0; slot <= 3; slot++)
	{
		ASSERT_TRUE(old_filter.Push(MakeFrame(slot)));
		for (auto s : DrainSlots(old_filter))
		{
			emitted.push_back(s);
		}
	}
	EXPECT_EQ(emitted, (std::vector<int64_t>{0, 1, 2}));
	ASSERT_NE(old_filter.Flush(), nullptr);
	EXPECT_EQ(old_filter.GetNextPts(), 4);

	// The new filter inherits the slot position; frames 4 and 5 were lost
	// around the swap, so its first frame arrives at slot 6
	auto new_filter = MakeFilter();
	new_filter.SetContinuationPts(old_filter.GetNextPts());

	ASSERT_TRUE(new_filter.Push(MakeFrame(6)));
	ASSERT_TRUE(new_filter.Push(MakeFrame(7)));

	// The hole is refilled with copies of the first frame after it
	EXPECT_EQ(DrainSlots(new_filter), (std::vector<int64_t>{4, 5, 6}));
}

TEST(FilterFpsTest, ContinuationReAnchorsOnRealDiscontinuity)
{
	auto filter = MakeFilter();
	filter.SetContinuationPts(4);

	// Far ahead of the inherited position: not a swap hole but a jump
	ASSERT_TRUE(filter.Push(MakeFrame(25)));
	ASSERT_TRUE(filter.Push(MakeFrame(26)));

	EXPECT_EQ(DrainSlots(filter), (std::vector<int64_t>{25}));
}

TEST(FilterFpsTest, ContinuationReAnchorsBackward)
{
	auto filter = MakeFilter();
	filter.SetContinuationPts(10);

	// Behind the inherited position: the timeline restarted
	ASSERT_TRUE(filter.Push(MakeFrame(5)));
	ASSERT_TRUE(filter.Push(MakeFrame(6)));

	EXPECT_EQ(DrainSlots(filter), (std::vector<int64_t>{5}));
}
