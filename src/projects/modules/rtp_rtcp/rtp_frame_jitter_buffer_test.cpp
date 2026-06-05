//==============================================================================
//
//  OvenMediaEngine - Unit Tests
//
//  Covers: RtpFrame completeness, RtpFrameJitterBuffer hold/advance behavior
//
//==============================================================================
#include <gtest/gtest.h>
#include <thread>

#include "rtp_frame_jitter_buffer.h"
#include "rtp_packet.h"

namespace
{
constexpr uint32_t kTimestamp = 90000;
constexpr uint32_t kClockRate = 90000;
constexpr uint32_t kSsrc = 0x12345678;

std::shared_ptr<RtpPacket> MakeStampedPacket(uint16_t seq, bool first, bool last, uint32_t ts = kTimestamp)
{
	auto p = std::make_shared<RtpPacket>();
	p->SetPayloadType(96);
	p->SetSequenceNumber(seq);
	p->SetTimestamp(ts);
	p->SetSsrc(kSsrc);
	p->SetMarker(last);
	uint8_t pl[4] = {0x21, 0x00, 0x00, 0x00};
	p->SetPayload(pl, sizeof(pl));
	// Simulate the codec fallback path: a NAL/unit start (StartOfUnit). A
	// multi-NAL access unit sets it on several packets; the buffer keeps the
	// lowest as the frame start.
	p->SetStartOfUnit(first);
	p->SetLastPacketOfFrame(last);
	return p;
}
}  // namespace

// ---- RtpFrame ----

TEST(RtpFrame, CompleteWhenStartEndAndAllSeqsPresent)
{
	RtpFrame frame(kTimestamp);
	frame.InsertPacket(MakeStampedPacket(100, /*first=*/true, false));
	frame.InsertPacket(MakeStampedPacket(101, false, false));
	frame.InsertPacket(MakeStampedPacket(102, false, /*last=*/true));
	EXPECT_TRUE(frame.IsCompleted());
}

TEST(RtpFrame, IncompleteWhenStartMissing)
{
	RtpFrame frame(kTimestamp);
	frame.InsertPacket(MakeStampedPacket(101, false, false));
	frame.InsertPacket(MakeStampedPacket(102, false, /*last=*/true));
	EXPECT_FALSE(frame.IsCompleted());
}

TEST(RtpFrame, IncompleteWhenEndMissing)
{
	RtpFrame frame(kTimestamp);
	frame.InsertPacket(MakeStampedPacket(100, /*first=*/true, false));
	frame.InsertPacket(MakeStampedPacket(101, false, false));
	EXPECT_FALSE(frame.IsCompleted());
}

TEST(RtpFrame, IncompleteWhenMiddleSeqMissing)
{
	RtpFrame frame(kTimestamp);
	frame.InsertPacket(MakeStampedPacket(100, /*first=*/true, false));
	frame.InsertPacket(MakeStampedPacket(102, false, /*last=*/true));
	EXPECT_FALSE(frame.IsCompleted());
}

TEST(RtpFrame, CompletesAcrossSeqWrap)
{
	RtpFrame frame(kTimestamp);
	frame.InsertPacket(MakeStampedPacket(65534, /*first=*/true, false));
	frame.InsertPacket(MakeStampedPacket(65535, false, false));
	frame.InsertPacket(MakeStampedPacket(0, false, false));
	frame.InsertPacket(MakeStampedPacket(1, false, /*last=*/true));
	EXPECT_TRUE(frame.IsCompleted());
}

TEST(RtpFrame, KeepsEarliestStartWhenMultipleFirstFlags)
{
	// A multi-NAL access unit flags several packets as "first" (e.g. STAP-A
	// then FU-A start). The earliest must win, otherwise the frame is judged
	// from the later NAL and never reaches its true packet count.
	RtpFrame frame(kTimestamp);
	frame.InsertPacket(MakeStampedPacket(100, /*first=*/true, false));   // STAP-A (SPS/PPS)
	frame.InsertPacket(MakeStampedPacket(101, /*first=*/true, false));   // FU-A IDR start
	frame.InsertPacket(MakeStampedPacket(102, false, /*last=*/true));
	EXPECT_TRUE(frame.IsCompleted());
	EXPECT_EQ(frame.GetFirstSequenceNumber(), 100);
}

TEST(RtpFrame, KeepsEarliestStartAcrossReorder)
{
	// Same as above but the later NAL's packet arrives first.
	RtpFrame frame(kTimestamp);
	frame.InsertPacket(MakeStampedPacket(101, /*first=*/true, false));
	frame.InsertPacket(MakeStampedPacket(100, /*first=*/true, false));
	frame.InsertPacket(MakeStampedPacket(102, false, /*last=*/true));
	EXPECT_TRUE(frame.IsCompleted());
	EXPECT_EQ(frame.GetFirstSequenceNumber(), 100);
}

TEST(RtpFrame, GetMaxReceivedSeqTracksHighest)
{
	RtpFrame frame(kTimestamp);
	frame.InsertPacket(MakeStampedPacket(100, true, false));
	EXPECT_EQ(frame.GetMaxReceivedSeq(), 100);
	frame.InsertPacket(MakeStampedPacket(105, false, false));
	EXPECT_EQ(frame.GetMaxReceivedSeq(), 105);
	frame.InsertPacket(MakeStampedPacket(103, false, false));   // older
	EXPECT_EQ(frame.GetMaxReceivedSeq(), 105);
}

// ---- RtpFrameJitterBuffer (legacy: no hold provider) ----

TEST(RtpFrameJitterBuffer, EmitsCompleteFrameImmediately)
{
	RtpFrameJitterBuffer buf;
	buf.SetClockRate(kClockRate);
	buf.InsertPacket(MakeStampedPacket(100, true, false));
	buf.InsertPacket(MakeStampedPacket(101, false, true));
	EXPECT_TRUE(buf.HasAvailableFrame());
	auto f = buf.PopAvailableFrame();
	ASSERT_NE(f, nullptr);
	EXPECT_EQ(f->Timestamp(), kTimestamp);
}

TEST(RtpFrameJitterBuffer, IncompleteHeadHoldsLaterCompleteFrame_Legacy)
{
	// Without a hold_ms_provider the legacy "discard incomplete predecessor
	// only on next completed" semantics apply: F1 incomplete, F2 complete ->
	// HasAvailableFrame burns out F1 and returns F2.
	RtpFrameJitterBuffer buf;
	buf.SetClockRate(kClockRate);
	buf.InsertPacket(MakeStampedPacket(100, true, false, /*ts=*/kTimestamp));  // F1 first
	// (F1 marker missing on purpose.)
	buf.InsertPacket(MakeStampedPacket(102, true, true, /*ts=*/kTimestamp + 3000));   // F2 single packet
	EXPECT_TRUE(buf.HasAvailableFrame());
	auto f = buf.PopAvailableFrame();
	ASSERT_NE(f, nullptr);
	EXPECT_EQ(f->Timestamp(), kTimestamp + 3000);   // F2 emitted, F1 discarded
}

// ---- RtpFrameJitterBuffer with NACK hold ----

TEST(RtpFrameJitterBuffer, HoldsIncompleteHeadUntilTimeout)
{
	RtpFrameJitterBuffer buf;
	buf.SetClockRate(kClockRate);
	buf.SetHoldMsProvider([] { return 50u; });
	buf.InsertPacket(MakeStampedPacket(100, true, false));   // F1 first only
	buf.InsertPacket(MakeStampedPacket(102, true, true, kTimestamp + 3000));   // F2 single packet

	// Within hold: F1 still present, F2 should not be emitted yet.
	EXPECT_FALSE(buf.HasAvailableFrame());

	// Effective hold = provider(50) + frame-interval mean(0) + 4*dev(seed 50)
	// ~= 250ms, so sleep well past it before expecting F1 to be discarded.
	std::this_thread::sleep_for(std::chrono::milliseconds(320));
	EXPECT_TRUE(buf.HasAvailableFrame());
}

TEST(RtpFrameJitterBuffer, MaxHoldCapsTotalHold)
{
	// Without a cap the hold would be provider(50) + 4*dev(seed 50) ~= 250ms.
	// SetMaxHoldMs(60) caps the total, so the incomplete head is discarded and
	// the later frame flows well before 250ms.
	RtpFrameJitterBuffer buf;
	buf.SetClockRate(kClockRate);
	buf.SetHoldMsProvider([] { return 50u; });
	buf.SetMaxHoldMs(60);
	buf.InsertPacket(MakeStampedPacket(100, true, false));   // F1 incomplete (no end)
	buf.InsertPacket(MakeStampedPacket(102, true, true, kTimestamp + 3000));   // F2 complete

	EXPECT_FALSE(buf.HasAvailableFrame());   // within the 60ms cap
	std::this_thread::sleep_for(std::chrono::milliseconds(90));
	EXPECT_TRUE(buf.HasAvailableFrame());    // released at the capped hold, far below 250ms
}

TEST(RtpFrameJitterBuffer, AdvanceProcessedSeqFiresOnEmit)
{
	RtpFrameJitterBuffer buf;
	buf.SetClockRate(kClockRate);
	std::optional<uint16_t> last_advanced;
	buf.SetOnProcessedSeqAdvance([&](uint16_t s) { last_advanced = s; });

	buf.InsertPacket(MakeStampedPacket(100, true, false));
	buf.InsertPacket(MakeStampedPacket(101, false, true));
	buf.PopAvailableFrame();

	ASSERT_TRUE(last_advanced.has_value());
	EXPECT_EQ(*last_advanced, 101);
}

TEST(RtpFrameJitterBuffer, HoldsHeadCompleteUntilLowerPendingResolves)
{
	// Scenario from production: frame F+1 entirely lost (object never built).
	// F+2 arrives as a single-packet frame and would emit immediately, but
	// NackGen has seq 28254 pending (NACK in flight for the lost F+1 packet).
	// HasAvailableFrame must hold F+2 until the pending recovers or expires.
	RtpFrameJitterBuffer buf;
	buf.SetClockRate(kClockRate);
	buf.SetHoldMsProvider([] { return 200u; });
	std::optional<uint16_t> lowest;
	buf.SetLowestPendingSeqProvider([&] { return lowest; });

	buf.InsertPacket(MakeStampedPacket(/*seq=*/28255, /*first=*/true, /*last=*/true, kTimestamp));

	lowest = 28254;   // NackGen has prior packet pending
	EXPECT_FALSE(buf.HasAvailableFrame()) << "must hold while lower seq still pending";

	lowest = std::nullopt;   // recovered (or NackGen dropped)
	EXPECT_TRUE(buf.HasAvailableFrame());
}

TEST(RtpFrameJitterBuffer, HeadHoldExpiresEventually)
{
	RtpFrameJitterBuffer buf;
	buf.SetClockRate(kClockRate);
	buf.SetHoldMsProvider([] { return 30u; });
	std::optional<uint16_t> lowest = 28254;
	buf.SetLowestPendingSeqProvider([&] { return lowest; });

	buf.InsertPacket(MakeStampedPacket(28255, true, true, kTimestamp));

	EXPECT_FALSE(buf.HasAvailableFrame());
	// Effective hold = provider(30) + frame-interval mean(0) + 4*dev(seed 50)
	// ~= 230ms; sleep past it so the head releases despite the lower pending.
	std::this_thread::sleep_for(std::chrono::milliseconds(320));
	EXPECT_TRUE(buf.HasAvailableFrame()) << "hold should release once timer elapses even with lower pending";
}

TEST(RtpFrameJitterBuffer, DropsLatePacketForProcessedTimestamp)
{
	RtpFrameJitterBuffer buf;
	buf.SetClockRate(kClockRate);

	buf.InsertPacket(MakeStampedPacket(100, true, true, kTimestamp));
	auto f = buf.PopAvailableFrame();
	ASSERT_NE(f, nullptr);

	// Late RTX-style packet for the same (already-processed) timestamp.
	bool ok = buf.InsertPacket(MakeStampedPacket(100, true, true, kTimestamp));
	EXPECT_FALSE(ok);
}
