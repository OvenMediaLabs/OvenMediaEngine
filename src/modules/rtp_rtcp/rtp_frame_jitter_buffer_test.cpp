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
#include "rtp_nack_generator.h"
#include "rtp_packet.h"

namespace
{
constexpr uint32_t kTimestamp = 90000;
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

TEST(RtpFrame, KeyframeFlagComesFromStartPacket)
{
	RtpFrame frame(kTimestamp);
	auto start = MakeStampedPacket(100, true, false);
	start->SetKeyframe(true);
	frame.InsertPacket(MakeStampedPacket(101, false, true));   // end arrives first, not a keyframe packet
	EXPECT_FALSE(frame.IsKeyframe());
	frame.InsertPacket(start);
	EXPECT_TRUE(frame.IsKeyframe());
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
	buf.SetHoldMsProvider([] { return 50u; });
	buf.InsertPacket(MakeStampedPacket(100, true, false));   // F1 first only
	buf.InsertPacket(MakeStampedPacket(102, true, true, kTimestamp + 3000));   // F2 single packet

	// Within hold: F1 still present, F2 should not be emitted yet.
	EXPECT_FALSE(buf.HasAvailableFrame());

	// Past the 50ms hold with no further F1 packet: F1 is discarded, F2 flows.
	std::this_thread::sleep_for(std::chrono::milliseconds(90));
	EXPECT_TRUE(buf.HasAvailableFrame());
}

// A large frame crawling in over a slow uplink keeps arriving past the hold
// measured from its first packet. It must not be discarded while packets keep
// coming, and it is emitted once complete.
TEST(RtpFrameJitterBuffer, SlowlyArrivingFrameIsNotDiscarded)
{
	RtpFrameJitterBuffer buf;
	buf.SetHoldMsProvider([] { return 50u; });
	buf.InsertPacket(MakeStampedPacket(100, true, false));                     // F1 start
	buf.InsertPacket(MakeStampedPacket(110, true, true, kTimestamp + 3000));   // F2 complete, waits behind F1

	// 5 more F1 packets, 40ms apart: 200ms since F1's first packet, far past
	// the 50ms hold, but never 50ms since its last packet.
	for (uint16_t seq = 101; seq <= 105; seq++)
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(40));
		buf.InsertPacket(MakeStampedPacket(seq, false, false));
		EXPECT_FALSE(buf.HasAvailableFrame()) << "F1 still arriving at seq " << seq;
	}

	buf.InsertPacket(MakeStampedPacket(106, false, true));                     // F1 end
	ASSERT_TRUE(buf.HasAvailableFrame());
	auto frame = buf.PopAvailableFrame();
	ASSERT_NE(frame, nullptr);
	EXPECT_EQ(frame->Timestamp(), kTimestamp);                                 // F1, not F2
}

// The hold clock restarts on every packet of the frame: a frame with a gap is
// discarded hold_ms after its last packet, not after its first.
TEST(RtpFrameJitterBuffer, HoldIsMeasuredFromLastPacket)
{
	RtpFrameJitterBuffer buf;
	buf.SetHoldMsProvider([] { return 50u; });
	buf.InsertPacket(MakeStampedPacket(100, true, false));                     // F1 start
	buf.InsertPacket(MakeStampedPacket(110, true, true, kTimestamp + 3000));   // F2 complete

	std::this_thread::sleep_for(std::chrono::milliseconds(40));
	buf.InsertPacket(MakeStampedPacket(102, false, true));                     // F1 end, seq 101 missing

	std::this_thread::sleep_for(std::chrono::milliseconds(30));                // 70ms since first, 30ms since last
	EXPECT_FALSE(buf.HasAvailableFrame());

	std::this_thread::sleep_for(std::chrono::milliseconds(60));                // 90ms since last packet
	EXPECT_TRUE(buf.HasAvailableFrame());                                      // F1 discarded, F2 flows
}

TEST(RtpFrameJitterBuffer, AdvanceProcessedSeqFiresOnEmit)
{
	RtpFrameJitterBuffer buf;
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
	buf.SetHoldMsProvider([] { return 30u; });
	std::optional<uint16_t> lowest = 28254;
	buf.SetLowestPendingSeqProvider([&] { return lowest; });

	buf.InsertPacket(MakeStampedPacket(28255, true, true, kTimestamp));

	EXPECT_FALSE(buf.HasAvailableFrame());
	// Sleep past the 30ms hold so the head releases despite the lower pending.
	std::this_thread::sleep_for(std::chrono::milliseconds(90));
	EXPECT_TRUE(buf.HasAvailableFrame()) << "hold should release once timer elapses even with lower pending";
}

// Every frame the buffer gives up on is reported, including several stale
// frames discarded in one pass.
TEST(RtpFrameJitterBuffer, EveryGiveUpIsReported)
{
	RtpFrameJitterBuffer buf;
	buf.SetHoldMsProvider([] { return 50u; });
	int discards = 0;
	buf.SetOnFrameDiscarded([&](bool) { discards++; });

	buf.InsertPacket(MakeStampedPacket(100, true, false));                     // F1 start only
	buf.InsertPacket(MakeStampedPacket(110, true, false, kTimestamp + 3000));  // F2 start only
	buf.InsertPacket(MakeStampedPacket(120, true, true, kTimestamp + 6000));   // F3 complete
	EXPECT_FALSE(buf.HasAvailableFrame());
	EXPECT_EQ(discards, 0);

	std::this_thread::sleep_for(std::chrono::milliseconds(90));
	EXPECT_TRUE(buf.HasAvailableFrame());                                      // F1 and F2 discarded, F3 flows
	EXPECT_EQ(discards, 2);
}

// Releasing a complete head over a lost earlier frame is a give-up too.
TEST(RtpFrameJitterBuffer, ReleaseOverLostFrameIsReported)
{
	RtpFrameJitterBuffer buf;
	buf.SetHoldMsProvider([] { return 30u; });
	std::optional<uint16_t> lowest = 28254;
	buf.SetLowestPendingSeqProvider([&] { return lowest; });
	int discards = 0;
	std::optional<bool> keyframe_arriving;
	buf.SetOnFrameDiscarded([&](bool arriving) { discards++; keyframe_arriving = arriving; });

	buf.InsertPacket(MakeStampedPacket(28255, true, true, kTimestamp));
	EXPECT_FALSE(buf.HasAvailableFrame());
	EXPECT_EQ(discards, 0);

	std::this_thread::sleep_for(std::chrono::milliseconds(90));
	EXPECT_TRUE(buf.HasAvailableFrame());
	EXPECT_EQ(discards, 1);
	ASSERT_TRUE(keyframe_arriving.has_value());
	EXPECT_FALSE(*keyframe_arriving);                                          // the released head is a delta frame
}

// The report says whether a keyframe is waiting in the buffer behind the
// given-up frame, complete or still arriving.
TEST(RtpFrameJitterBuffer, ReportTellsWhetherKeyframeIsArriving)
{
	RtpFrameJitterBuffer buf;
	buf.SetHoldMsProvider([] { return 50u; });
	std::optional<bool> keyframe_arriving;
	buf.SetOnFrameDiscarded([&](bool arriving) { keyframe_arriving = arriving; });

	buf.InsertPacket(MakeStampedPacket(100, true, false));                     // F1 start only
	std::this_thread::sleep_for(std::chrono::milliseconds(90));
	auto keyframe_start = MakeStampedPacket(110, true, false, kTimestamp + 3000);   // F2 keyframe, still arriving
	keyframe_start->SetKeyframe(true);
	buf.InsertPacket(keyframe_start);
	EXPECT_FALSE(buf.HasAvailableFrame());                                     // F1 discarded, F2 incomplete
	ASSERT_TRUE(keyframe_arriving.has_value());
	EXPECT_TRUE(*keyframe_arriving);
}

// The keyframe count behind the report counts a frame once however many of
// its packets carry the mark, and drops again when the keyframe leaves.
TEST(RtpFrameJitterBuffer, KeyframeCountFollowsFramesInAndOut)
{
	RtpFrameJitterBuffer buf;
	buf.SetHoldMsProvider([] { return 50u; });
	std::optional<bool> keyframe_arriving;
	buf.SetOnFrameDiscarded([&](bool arriving) { keyframe_arriving = arriving; });

	buf.InsertPacket(MakeStampedPacket(100, true, false));                     // F1 start only
	std::this_thread::sleep_for(std::chrono::milliseconds(90));
	auto key_a = MakeStampedPacket(110, true, false, kTimestamp + 3000);       // F2 keyframe, two marked packets
	key_a->SetKeyframe(true);
	auto key_b = MakeStampedPacket(111, false, true, kTimestamp + 3000);
	key_b->SetKeyframe(true);
	buf.InsertPacket(key_a);
	buf.InsertPacket(key_b);
	ASSERT_TRUE(buf.HasAvailableFrame());                                      // F1 given up, F2 complete
	ASSERT_TRUE(keyframe_arriving.has_value());
	EXPECT_TRUE(*keyframe_arriving);
	ASSERT_NE(buf.PopAvailableFrame(), nullptr);                               // the keyframe leaves

	keyframe_arriving.reset();
	buf.InsertPacket(MakeStampedPacket(120, true, false, kTimestamp + 6000));  // F3 start only
	std::this_thread::sleep_for(std::chrono::milliseconds(90));
	buf.InsertPacket(MakeStampedPacket(130, true, true, kTimestamp + 9000));   // F4 delta, complete
	EXPECT_TRUE(buf.HasAvailableFrame());                                      // F3 given up, F4 flows
	ASSERT_TRUE(keyframe_arriving.has_value());
	EXPECT_FALSE(*keyframe_arriving);                                          // counted once, and gone
}

TEST(RtpFrameJitterBuffer, ReportSaysNoKeyframeWhenOnlyDeltaFramesFollow)
{
	RtpFrameJitterBuffer buf;
	buf.SetHoldMsProvider([] { return 50u; });
	std::optional<bool> keyframe_arriving;
	buf.SetOnFrameDiscarded([&](bool arriving) { keyframe_arriving = arriving; });

	buf.InsertPacket(MakeStampedPacket(100, true, false));                     // F1 start only
	std::this_thread::sleep_for(std::chrono::milliseconds(90));
	buf.InsertPacket(MakeStampedPacket(110, true, true, kTimestamp + 3000));   // F2 delta, complete
	EXPECT_TRUE(buf.HasAvailableFrame());                                      // F1 discarded, F2 flows
	ASSERT_TRUE(keyframe_arriving.has_value());
	EXPECT_FALSE(*keyframe_arriving);
}

// A frame that never ends cannot pin the buffer: past the packet budget the
// incomplete head is given up and later frames flow.
TEST(RtpFrameJitterBuffer, PacketBudgetGivesUpNeverEndingHead)
{
	RtpFrameJitterBuffer buf;
	buf.SetHoldMsProvider([] { return 600u; });
	int discards = 0;
	buf.SetOnFrameDiscarded([&](bool) { discards++; });

	for (size_t i = 0; i <= RtpFrameJitterBuffer::MAX_PACKETS; i++)
	{
		buf.InsertPacket(MakeStampedPacket(static_cast<uint16_t>(i), i == 0, false));   // F1 keeps growing, never ends
	}
	EXPECT_EQ(discards, 1);
	EXPECT_TRUE(buf.IsEmpty());

	buf.InsertPacket(MakeStampedPacket(3000, true, true, kTimestamp + 3000));   // F2 complete
	EXPECT_TRUE(buf.HasAvailableFrame());
}

// A keyframe of several thousand packets (4K at 50 Mbps) is far inside the
// budget and must be delivered whole.
TEST(RtpFrameJitterBuffer, LargeFrameWithinBudgetIsDelivered)
{
	RtpFrameJitterBuffer buf;
	buf.SetHoldMsProvider([] { return 600u; });
	int discards = 0;
	buf.SetOnFrameDiscarded([&](bool) { discards++; });

	constexpr uint16_t kPackets = 4000;
	for (uint16_t i = 0; i < kPackets; i++)
	{
		buf.InsertPacket(MakeStampedPacket(i, i == 0, i == kPackets - 1));
	}
	EXPECT_EQ(discards, 0);
	auto frame = buf.PopAvailableFrame();
	ASSERT_NE(frame, nullptr);
	EXPECT_EQ(frame->PacketCount(), kPackets);
}

TEST(RtpFrameJitterBuffer, PacketBudgetReleasesFramesStuckBehindHead)
{
	RtpFrameJitterBuffer buf;
	buf.SetHoldMsProvider([] { return 600u; });
	int discards = 0;
	buf.SetOnFrameDiscarded([&](bool) { discards++; });

	buf.InsertPacket(MakeStampedPacket(100, true, false));   // F1 start only, blocks everything behind
	for (size_t i = 1; i <= RtpFrameJitterBuffer::MAX_PACKETS; i++)
	{
		buf.InsertPacket(MakeStampedPacket(static_cast<uint16_t>(100 + i), true, true, kTimestamp + 3000 * i));   // complete single-packet frames
	}
	EXPECT_EQ(discards, 1);                                   // F1 given up at the budget
	EXPECT_TRUE(buf.HasAvailableFrame());                     // the frames behind it flow
}

// A complete head waiting for a lower pending seq is released once the
// buffer is over budget, not only when the hold expires.
TEST(RtpFrameJitterBuffer, PacketBudgetReleasesHeadHeldForLowerPending)
{
	RtpFrameJitterBuffer buf;
	buf.SetHoldMsProvider([] { return 10000u; });
	std::optional<uint16_t> lowest = 90;
	buf.SetLowestPendingSeqProvider([&] { return lowest; });

	for (size_t i = 0; i <= RtpFrameJitterBuffer::MAX_PACKETS; i++)
	{
		buf.InsertPacket(MakeStampedPacket(static_cast<uint16_t>(100 + i), true, true, kTimestamp + 3000 * i));   // complete frames
	}
	EXPECT_TRUE(buf.HasAvailableFrame());
}

// A frame that lost its start before the first seq the NACK generator saw
// can never be repaired, so it is given up once the reorder dwell has passed,
// but not before: a merely reordered start may still arrive within it.
TEST(RtpFrameJitterBuffer, GivesUpFrameWhoseStartPrecedesTheStream)
{
	RtpFrameJitterBuffer buf;
	buf.SetHoldMsProvider([] { return 10000u; });
	buf.SetFirstObservedSeqProvider([] { return std::optional<uint16_t>(100); });
	int discards = 0;
	buf.SetOnFrameDiscarded([&](bool) { discards++; });

	buf.InsertPacket(MakeStampedPacket(100, false, true));                    // F1: first seq of the stream, start missing
	buf.InsertPacket(MakeStampedPacket(110, true, true, kTimestamp + 3000));  // F2 begins right away
	EXPECT_FALSE(buf.HasAvailableFrame());                                    // within the reorder dwell F1 still waits
	EXPECT_EQ(discards, 0);

	std::this_thread::sleep_for(std::chrono::milliseconds(RtpNackGenerator::INITIAL_NACK_DWELL_MS + 10));
	EXPECT_TRUE(buf.HasAvailableFrame());                                     // dwell over: F1 given up, F2 flows
	EXPECT_EQ(discards, 1);
}

// Without a later frame the give-up still happens once the reorder dwell
// has passed, so a large first keyframe does not hold the request back.
TEST(RtpFrameJitterBuffer, GivesUpUnrepairableFrameAfterReorderDwell)
{
	RtpFrameJitterBuffer buf;
	buf.SetHoldMsProvider([] { return 10000u; });
	buf.SetFirstObservedSeqProvider([] { return std::optional<uint16_t>(100); });
	int discards = 0;
	buf.SetOnFrameDiscarded([&](bool) { discards++; });

	buf.InsertPacket(MakeStampedPacket(100, false, false));                   // still arriving, start missing
	std::this_thread::sleep_for(std::chrono::milliseconds(RtpNackGenerator::INITIAL_NACK_DWELL_MS + 10));
	buf.InsertPacket(MakeStampedPacket(101, false, false));
	EXPECT_FALSE(buf.HasAvailableFrame());                                    // nothing complete to emit
	EXPECT_EQ(discards, 1);                                                   // but the hopeless frame is gone
	EXPECT_TRUE(buf.IsEmpty());
}

// Past the first frame the shortcut must stay off: a start lost far along the
// stream is requestable even though the 16-bit distance to the first seq wrapped.
TEST(RtpFrameJitterBuffer, KeepsStartlessFrameBeyondHalfSequenceCycle)
{
	RtpFrameJitterBuffer buf;
	buf.SetHoldMsProvider([] { return 10000u; });
	buf.SetFirstObservedSeqProvider([] { return std::optional<uint16_t>(100); });
	int discards = 0;
	buf.SetOnFrameDiscarded([&](bool) { discards++; });

	buf.InsertPacket(MakeStampedPacket(100, true, true));                     // first frame, complete
	ASSERT_NE(buf.PopAvailableFrame(), nullptr);

	buf.InsertPacket(MakeStampedPacket(40000, false, true, kTimestamp + 3000));   // start 39999 lost, seq distance wrapped
	std::this_thread::sleep_for(std::chrono::milliseconds(RtpNackGenerator::INITIAL_NACK_DWELL_MS + 10));
	buf.InsertPacket(MakeStampedPacket(40010, true, true, kTimestamp + 6000));
	EXPECT_FALSE(buf.HasAvailableFrame());                                    // the frame waits for NACK
	EXPECT_EQ(discards, 0);
}

TEST(RtpFrameJitterBuffer, KeepsStartlessFrameWhoseGapIsRequestable)
{
	RtpFrameJitterBuffer buf;
	buf.SetHoldMsProvider([] { return 10000u; });
	buf.SetFirstObservedSeqProvider([] { return std::optional<uint16_t>(95); });   // the stream was seen from 95
	int discards = 0;
	buf.SetOnFrameDiscarded([&](bool) { discards++; });

	buf.InsertPacket(MakeStampedPacket(100, false, true));                    // start (96..99) lost but requestable
	buf.InsertPacket(MakeStampedPacket(110, true, true, kTimestamp + 3000));
	EXPECT_FALSE(buf.HasAvailableFrame());                                    // F1 waits for NACK within the hold
	EXPECT_EQ(discards, 0);
}

TEST(RtpFrameJitterBuffer, DropsLatePacketForProcessedTimestamp)
{
	RtpFrameJitterBuffer buf;

	buf.InsertPacket(MakeStampedPacket(100, true, true, kTimestamp));
	auto f = buf.PopAvailableFrame();
	ASSERT_NE(f, nullptr);

	// Late RTX-style packet for the same (already-processed) timestamp.
	bool ok = buf.InsertPacket(MakeStampedPacket(100, true, true, kTimestamp));
	EXPECT_FALSE(ok);
}
