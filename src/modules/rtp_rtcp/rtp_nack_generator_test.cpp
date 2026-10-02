//==============================================================================
//
//  OvenMediaEngine - Unit Tests
//
//  Covers: RtpNackGenerator
//
//==============================================================================
#include <gtest/gtest.h>
#include <thread>

#include "rtp_nack_generator.h"

namespace
{
constexpr uint32_t kTrackId = 1;
constexpr uint32_t kSsrc = 0xDEADBEEF;
}  // namespace

// Bootstrap: first seq sets the highest watermark, no gap reported yet.
TEST(RtpNackGenerator, Bootstrap)
{
	RtpNackGenerator gen(kTrackId, kSsrc);
	gen.OnPacketReceived(100);
	EXPECT_FALSE(gen.GetLowestPendingSeq().has_value());
}

// Single-seq gap: 100, 102 -> [101] pending after dwell.
TEST(RtpNackGenerator, SingleGapAddsToPending)
{
	RtpNackGenerator gen(kTrackId, kSsrc);
	gen.OnPacketReceived(100);
	gen.OnPacketReceived(102);
	ASSERT_TRUE(gen.GetLowestPendingSeq().has_value());
	EXPECT_EQ(*gen.GetLowestPendingSeq(), 101);
}

// Multi-seq gap: 100, 105 -> [101, 102, 103, 104].
TEST(RtpNackGenerator, MultiSeqGap)
{
	RtpNackGenerator gen(kTrackId, kSsrc);
	gen.OnPacketReceived(100);
	gen.OnPacketReceived(105);
	ASSERT_TRUE(gen.GetLowestPendingSeq().has_value());
	EXPECT_EQ(*gen.GetLowestPendingSeq(), 101);
}

// Dwell absorbs a quick reorder: 100, 102, 101 (within 10ms) -> no NACK fired.
TEST(RtpNackGenerator, ReorderAbsorbedWithinDwell)
{
	RtpNackGenerator gen(kTrackId, kSsrc);
	gen.OnPacketReceived(100);
	gen.OnPacketReceived(102);
	gen.OnPacketReceived(101);   // reorder filled the gap immediately
	auto ids = gen.BuildPendingNack();
	EXPECT_TRUE(ids.empty());
	EXPECT_FALSE(gen.GetLowestPendingSeq().has_value());
}

// After dwell expires, BuildPendingNack returns the gap.
TEST(RtpNackGenerator, NackFiresAfterDwell)
{
	RtpNackGenerator gen(kTrackId, kSsrc);
	gen.OnPacketReceived(100);
	gen.OnPacketReceived(102);
	std::this_thread::sleep_for(std::chrono::milliseconds(RtpNackGenerator::INITIAL_NACK_DWELL_MS + 5));
	auto ids = gen.BuildPendingNack();
	ASSERT_EQ(ids.size(), 1u);
	EXPECT_EQ(ids[0], 101);
}

// Recovery: missing seq arrives after NACK -> pending cleared.
TEST(RtpNackGenerator, RecoverClearsPending)
{
	RtpNackGenerator gen(kTrackId, kSsrc);
	gen.OnPacketReceived(100);
	gen.OnPacketReceived(102);
	std::this_thread::sleep_for(std::chrono::milliseconds(RtpNackGenerator::INITIAL_NACK_DWELL_MS + 5));
	gen.BuildPendingNack();      // fires NACK for 101
	gen.OnPacketReceived(101);   // recovered
	EXPECT_FALSE(gen.GetLowestPendingSeq().has_value());
}

// DropPendingUpTo: explicit jitter-buffer-driven cleanup.
TEST(RtpNackGenerator, DropPendingUpToRemovesAtAndBelow)
{
	RtpNackGenerator gen(kTrackId, kSsrc);
	gen.OnPacketReceived(100);
	gen.OnPacketReceived(105);   // adds 101, 102, 103, 104
	gen.DropPendingUpTo(102);    // drops 101, 102
	ASSERT_TRUE(gen.GetLowestPendingSeq().has_value());
	EXPECT_EQ(*gen.GetLowestPendingSeq(), 103);
}

// Seq wrap: highest=65530, then 5 -> gap [65531..65535, 0..4].
TEST(RtpNackGenerator, SeqWrapDetectsGap)
{
	RtpNackGenerator gen(kTrackId, kSsrc);
	gen.OnPacketReceived(65530);
	gen.OnPacketReceived(5);
	ASSERT_TRUE(gen.GetLowestPendingSeq().has_value());
	EXPECT_EQ(*gen.GetLowestPendingSeq(), 65531);
}

// Old packet beyond expected window is ignored, not added as gap.
TEST(RtpNackGenerator, OldSeqDoesNotCreateGap)
{
	RtpNackGenerator gen(kTrackId, kSsrc);
	gen.OnPacketReceived(100);
	gen.OnPacketReceived(99);   // late / reorder; not in pending
	EXPECT_FALSE(gen.GetLowestPendingSeq().has_value());
}

// GetLowestPendingSeq returns the smallest pending seq.
TEST(RtpNackGenerator, LowestPendingTracksSmallest)
{
	RtpNackGenerator gen(kTrackId, kSsrc);
	gen.OnPacketReceived(100);
	gen.OnPacketReceived(110);   // adds 101..109
	ASSERT_TRUE(gen.GetLowestPendingSeq().has_value());
	EXPECT_EQ(*gen.GetLowestPendingSeq(), 101);

	gen.OnPacketReceived(101);   // recovers 101
	ASSERT_TRUE(gen.GetLowestPendingSeq().has_value());
	EXPECT_EQ(*gen.GetLowestPendingSeq(), 102);
}

// The hold is the configured value, nothing adaptive.
TEST(RtpNackGenerator, HoldIsTheConfiguredValue)
{
	RtpNackGenerator by_default(kTrackId, kSsrc);
	EXPECT_EQ(by_default.GetHoldMs(), RtpNackGenerator::HOLD_MS_DEFAULT);

	RtpNackGenerator configured(kTrackId, kSsrc, /*hold_ms=*/450);
	EXPECT_EQ(configured.GetHoldMs(), 450u);
}

// Build twice within the retry interval -> the seq is returned only once.
TEST(RtpNackGenerator, RetryNotDoubleFiredWithinInterval)
{
	RtpNackGenerator gen(kTrackId, kSsrc);
	gen.OnPacketReceived(100);
	gen.OnPacketReceived(102);
	std::this_thread::sleep_for(std::chrono::milliseconds(RtpNackGenerator::INITIAL_NACK_DWELL_MS + 5));
	auto first_round = gen.BuildPendingNack();
	ASSERT_EQ(first_round.size(), 1u);

	auto immediate_again = gen.BuildPendingNack();
	EXPECT_TRUE(immediate_again.empty()) << "should not retry before RETRY_INTERVAL_MS elapses";
}

// Once the retry interval elapsed the seq is requested again, and keeps being
// requested on the same cadence until it arrives.
TEST(RtpNackGenerator, RetryFiresOnFixedInterval)
{
	RtpNackGenerator gen(kTrackId, kSsrc);
	const auto interval = std::chrono::milliseconds(RtpNackGenerator::RETRY_INTERVAL_MS + 5);
	gen.OnPacketReceived(100);
	gen.OnPacketReceived(102);
	std::this_thread::sleep_for(std::chrono::milliseconds(RtpNackGenerator::INITIAL_NACK_DWELL_MS + 5));
	ASSERT_EQ(gen.BuildPendingNack().size(), 1u);  // initial NACK

	std::this_thread::sleep_for(interval);
	auto retry = gen.BuildPendingNack();
	ASSERT_EQ(retry.size(), 1u);
	EXPECT_EQ(retry[0], 101);

	std::this_thread::sleep_for(interval);
	ASSERT_EQ(gen.BuildPendingNack().size(), 1u);  // second retry

	gen.OnPacketReceived(101);                      // answered
	std::this_thread::sleep_for(interval);
	EXPECT_TRUE(gen.BuildPendingNack().empty());
}
