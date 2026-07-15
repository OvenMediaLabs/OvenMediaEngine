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

// Initial RTT seeding: GetRecommendedHoldMs uses INITIAL_RTT_GUESS_MS until
// the first sample lands, then EWMA-derived value. Result is clamped to
// at least HOLD_MIN_MS.
TEST(RtpNackGenerator, InitialRecommendedHoldUsesGuessClampedToMin)
{
	RtpNackGenerator gen(kTrackId, kSsrc);
	EXPECT_GE(gen.GetRecommendedHoldMs(), RtpNackGenerator::HOLD_MIN_MS);
}

// MaxHoldMs constructor argument clamps GetRecommendedHoldMs upper bound.
TEST(RtpNackGenerator, MaxHoldClamps)
{
	// Initial hold = dwell(10) + retries(5) * RTT_guess(15) + 4 * dev(0) = 85,
	// which exceeds the 80ms cap, so the clamp is exercised here.
	RtpNackGenerator gen(kTrackId, kSsrc, /*max_hold_ms=*/80);
	EXPECT_LE(gen.GetRecommendedHoldMs(), 80u);
	EXPECT_GE(gen.GetRecommendedHoldMs(), RtpNackGenerator::HOLD_MIN_MS);
}

// Retry interval respects ewma. Build twice within the interval -> only the
// initial entry returns the seq once.
TEST(RtpNackGenerator, RetryNotDoubleFiredWithinInterval)
{
	RtpNackGenerator gen(kTrackId, kSsrc);
	gen.OnPacketReceived(100);
	gen.OnPacketReceived(102);
	std::this_thread::sleep_for(std::chrono::milliseconds(RtpNackGenerator::INITIAL_NACK_DWELL_MS + 5));
	auto first_round = gen.BuildPendingNack();
	ASSERT_EQ(first_round.size(), 1u);

	auto immediate_again = gen.BuildPendingNack();
	EXPECT_TRUE(immediate_again.empty()) << "should not retry before retry_interval (ewma) elapses";
}
