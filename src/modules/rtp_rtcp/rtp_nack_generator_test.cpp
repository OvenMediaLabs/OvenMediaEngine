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
	// Initial hold = dwell(10) + retries(5) * RTT_guess(100) = 510, which
	// exceeds the 80ms cap, so the clamp is exercised here.
	RtpNackGenerator gen(kTrackId, kSsrc, /*max_hold_ms=*/80);
	EXPECT_LE(gen.GetRecommendedHoldMs(), 80u);
	EXPECT_GE(gen.GetRecommendedHoldMs(), RtpNackGenerator::HOLD_MIN_MS);
}

// Retry interval respects the smoothed round trip. Build twice within the
// interval -> only the initial entry returns the seq once.
TEST(RtpNackGenerator, RetryNotDoubleFiredWithinInterval)
{
	RtpNackGenerator gen(kTrackId, kSsrc);
	gen.OnPacketReceived(100);
	gen.OnPacketReceived(102);
	std::this_thread::sleep_for(std::chrono::milliseconds(RtpNackGenerator::INITIAL_NACK_DWELL_MS + 5));
	auto first_round = gen.BuildPendingNack();
	ASSERT_EQ(first_round.size(), 1u);

	auto immediate_again = gen.BuildPendingNack();
	EXPECT_TRUE(immediate_again.empty()) << "should not retry before retry_interval (ewma + 4 * dev) elapses";
}

// RTT longer than the initial retry interval: the first loss event needs a
// retry (no clean sample, backoff doubles), so on the next event the retry
// waits long enough for the RTX to land as a single-NACK sample and the hold
// grows toward the real round trip instead of staying locked at the guess.
TEST(RtpNackGenerator, BackoffLetsLongRttSampleLand)
{
	RtpNackGenerator gen(kTrackId, kSsrc, /*max_hold_ms=*/2000);
	const auto dwell = std::chrono::milliseconds(RtpNackGenerator::INITIAL_NACK_DWELL_MS + 5);
	// Longer than the initial retry interval (guess + margin floor), shorter
	// than one doubling of it.
	const auto rtt = std::chrono::milliseconds(
		static_cast<int64_t>(RtpNackGenerator::INITIAL_RTT_GUESS_MS) + RtpNackGenerator::MIN_RETRY_MARGIN_MS + 30);

	auto hold_before = gen.GetRecommendedHoldMs();

	// Event 1: RTX arrives after a retry already went out -> no sample.
	gen.OnPacketReceived(100);
	gen.OnPacketReceived(102);
	std::this_thread::sleep_for(dwell);
	ASSERT_EQ(gen.BuildPendingNack().size(), 1u);  // initial NACK
	std::this_thread::sleep_for(rtt);
	ASSERT_EQ(gen.BuildPendingNack().size(), 1u);  // retry fired, backoff doubles
	gen.OnPacketReceived(101);
	EXPECT_EQ(gen.GetRecommendedHoldMs(), hold_before);

	// Event 2: same RTT, but the backed-off interval outlasts it.
	gen.OnPacketReceived(103);
	gen.OnPacketReceived(105);
	std::this_thread::sleep_for(dwell);
	ASSERT_EQ(gen.BuildPendingNack().size(), 1u);  // initial NACK
	std::this_thread::sleep_for(rtt);
	EXPECT_TRUE(gen.BuildPendingNack().empty());   // no retry yet
	gen.OnPacketReceived(104);
	EXPECT_GT(gen.GetRecommendedHoldMs(), hold_before);
}

// A packet that shows up right after its NACK cannot be an answer to it (a
// copy the sender pushed on its own). Such a sample must not shrink the hold:
// the initial guess still sits in the window and the max ignores the outlier.
TEST(RtpNackGenerator, TinySampleDoesNotShrinkHold)
{
	RtpNackGenerator gen(kTrackId, kSsrc, /*max_hold_ms=*/2000);
	auto hold_before = gen.GetRecommendedHoldMs();

	gen.OnPacketReceived(100);
	gen.OnPacketReceived(102);
	std::this_thread::sleep_for(std::chrono::milliseconds(RtpNackGenerator::INITIAL_NACK_DWELL_MS + 5));
	ASSERT_EQ(gen.BuildPendingNack().size(), 1u);
	gen.OnPacketReceived(101);  // "answer" ~0ms after the NACK

	EXPECT_EQ(gen.GetRecommendedHoldMs(), hold_before);
}

// Once RTT_WINDOW_SIZE real samples landed, the initial guess has left the
// window and the hold follows the measured round trip.
TEST(RtpNackGenerator, WindowForgetsInitialGuess)
{
	RtpNackGenerator gen(kTrackId, kSsrc, /*max_hold_ms=*/2000);
	const auto dwell = std::chrono::milliseconds(RtpNackGenerator::INITIAL_NACK_DWELL_MS + 5);
	const auto rtt = std::chrono::milliseconds(30);
	auto hold_before = gen.GetRecommendedHoldMs();

	for (size_t i = 0; i < RtpNackGenerator::RTT_WINDOW_SIZE; i++)
	{
		uint16_t base = static_cast<uint16_t>(100 + 3 * i);
		gen.OnPacketReceived(base);
		gen.OnPacketReceived(static_cast<uint16_t>(base + 2));  // gap: base + 1
		std::this_thread::sleep_for(dwell);
		ASSERT_EQ(gen.BuildPendingNack().size(), 1u);
		std::this_thread::sleep_for(rtt);
		gen.OnPacketReceived(static_cast<uint16_t>(base + 1));  // clean ~30ms sample
	}

	EXPECT_LT(gen.GetRecommendedHoldMs(), hold_before);
}
