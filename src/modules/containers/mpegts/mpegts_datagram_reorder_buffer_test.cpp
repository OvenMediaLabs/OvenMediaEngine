//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2026 OvenMediaLabs. All rights reserved.
//
//==============================================================================
#include "mpegts_datagram_reorder_buffer.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <random>
#include <vector>

namespace
{
	constexpr uint16_t VIDEO_PID = 0x0100;
	constexpr uint16_t AUDIO_PID = 0x0101;

	// Builds a datagram (a contiguous run of 188-byte TS packets) from a list of (pid, cc) pairs.
	std::shared_ptr<const ov::Data> MakeDatagram(const std::vector<std::pair<uint16_t, uint8_t>> &packets,
												 bool discontinuity = false, bool transport_error = false)
	{
		std::vector<uint8_t> buffer(packets.size() * 188, 0x00);

		for (size_t i = 0; i < packets.size(); i++)
		{
			uint8_t *p		   = buffer.data() + (i * 188);
			const uint16_t pid = packets[i].first;
			const uint8_t cc   = packets[i].second;

			p[0]			   = 0x47;
			p[1]			   = static_cast<uint8_t>((transport_error ? 0x80 : 0x00) | ((pid >> 8) & 0x1F));
			p[2]			   = static_cast<uint8_t>(pid & 0xFF);

			if (discontinuity)
			{
				// Adaptation field + payload, with the discontinuity_indicator set.
				p[3] = static_cast<uint8_t>((0b11 << 4) | (cc & 0x0F));
				p[4] = 1;	  // adaptation_field_length
				p[5] = 0x80;  // discontinuity_indicator
			}
			else
			{
				// Payload only.
				p[3] = static_cast<uint8_t>((0b01 << 4) | (cc & 0x0F));
			}
		}

		return std::make_shared<ov::Data>(buffer.data(), buffer.size());
	}

	// A single-PID datagram carrying one packet with the given continuity counter.
	std::shared_ptr<const ov::Data> MakeVideo(uint8_t cc)
	{
		return MakeDatagram({{VIDEO_PID, cc}});
	}

	// A datagram carrying `count` consecutive video packets starting at continuity counter `base_cc`
	// (realistic framing: real UDP datagrams carry several TS packets, so the counter steps by `count`
	// per datagram).
	std::shared_ptr<const ov::Data> MakeVideoRun(uint8_t base_cc, int count)
	{
		std::vector<std::pair<uint16_t, uint8_t>> packets;
		for (int i = 0; i < count; i++)
		{
			packets.push_back({VIDEO_PID, static_cast<uint8_t>((base_cc + i) & 0x0F)});
		}
		return MakeDatagram(packets);
	}

	// Drives the buffer and maps delivered datagrams back to their logical index via pointer identity.
	class Harness
	{
	public:
		explicit Harness(mpegts::DatagramReorderBuffer *buffer)
			: _buffer(buffer)
		{
		}

		// Registers a datagram under a logical index and feeds it in one step.
		std::vector<int> Feed(int index, const std::shared_ptr<const ov::Data> &datagram)
		{
			_by_pointer[datagram.get()] = index;

			std::vector<std::shared_ptr<const ov::Data>> out;
			_buffer->Enqueue(datagram, &out);
			return ToIndices(out);
		}

		std::vector<int> FlushRemaining()
		{
			std::vector<std::shared_ptr<const ov::Data>> out;
			_buffer->Flush(&out);
			return ToIndices(out);
		}

	private:
		std::vector<int> ToIndices(const std::vector<std::shared_ptr<const ov::Data>> &out)
		{
			std::vector<int> indices;
			for (const auto &d : out)
			{
				auto it = _by_pointer.find(d.get());
				indices.push_back(it == _by_pointer.end() ? -1 : it->second);
			}
			return indices;
		}

		mpegts::DatagramReorderBuffer *_buffer;
		std::map<const ov::Data *, int> _by_pointer;
	};
}  // namespace

TEST(MpegTsDatagramReorderBuffer, InOrderPassthroughHasNoLatency)
{
	mpegts::DatagramReorderBuffer buffer;
	Harness h(&buffer);

	EXPECT_EQ(h.Feed(0, MakeVideo(0)), (std::vector<int>{0}));
	EXPECT_EQ(h.Feed(1, MakeVideo(1)), (std::vector<int>{1}));
	EXPECT_EQ(h.Feed(2, MakeVideo(2)), (std::vector<int>{2}));
	EXPECT_EQ(buffer.GetBufferedCount(), 0u);
}

TEST(MpegTsDatagramReorderBuffer, AdjacentSwapIsRestored)
{
	mpegts::DatagramReorderBuffer buffer;
	Harness h(&buffer);

	std::vector<int> delivered;
	auto append = [&delivered](const std::vector<int> &v) { delivered.insert(delivered.end(), v.begin(), v.end()); };

	append(h.Feed(0, MakeVideo(0)));  // in order
	append(h.Feed(2, MakeVideo(2)));  // ahead -> buffered
	append(h.Feed(1, MakeVideo(1)));  // fills the gap -> 1 then cascade 2
	append(h.Feed(3, MakeVideo(3)));

	EXPECT_EQ(delivered, (std::vector<int>{0, 1, 2, 3}));
	EXPECT_EQ(buffer.GetBufferedCount(), 0u);
}

TEST(MpegTsDatagramReorderBuffer, MultiStepReorderCascades)
{
	mpegts::DatagramReorderBuffer buffer;
	Harness h(&buffer);

	std::vector<int> delivered;
	auto append = [&delivered](const std::vector<int> &v) { delivered.insert(delivered.end(), v.begin(), v.end()); };

	// Arrival: 0, 3, 1, 2  ->  3 waits, then 1, then 2 fills and 3 cascades.
	append(h.Feed(0, MakeVideo(0)));
	append(h.Feed(3, MakeVideo(3)));
	append(h.Feed(1, MakeVideo(1)));
	append(h.Feed(2, MakeVideo(2)));

	EXPECT_EQ(delivered, (std::vector<int>{0, 1, 2, 3}));
}

TEST(MpegTsDatagramReorderBuffer, DepthFlushDeclaresLoss)
{
	// Small depth so the lost datagram is declared quickly.
	mpegts::DatagramReorderBuffer buffer(/*max_datagrams=*/3, /*timeout_msec=*/1000000);
	Harness h(&buffer);

	std::vector<int> delivered;
	auto append = [&delivered](const std::vector<int> &v) { delivered.insert(delivered.end(), v.begin(), v.end()); };

	append(h.Feed(0, MakeVideo(0)));  // delivered, expected -> 1
	append(h.Feed(1, MakeVideo(2)));  // cc 1 lost; this is cc 2 -> buffered
	append(h.Feed(2, MakeVideo(3)));  // buffered
	append(h.Feed(3, MakeVideo(4)));  // buffer depth reaches 3 -> flush: emit 2,3,4 in order

	EXPECT_EQ(delivered, (std::vector<int>{0, 1, 2, 3}));
	EXPECT_EQ(buffer.GetBufferedCount(), 0u);
}

TEST(MpegTsDatagramReorderBuffer, TimeoutFlushDeclaresLoss)
{
	int64_t now = 0;
	mpegts::DatagramReorderBuffer buffer(/*max_datagrams=*/16, /*timeout_msec=*/100,
										 /*clock=*/[&now]() { return now; });
	Harness h(&buffer);

	std::vector<int> delivered;
	auto append = [&delivered](const std::vector<int> &v) { delivered.insert(delivered.end(), v.begin(), v.end()); };

	append(h.Feed(0, MakeVideo(0)));  // delivered
	append(h.Feed(1, MakeVideo(2)));  // cc 1 lost; cc 2 buffered at t=0
	EXPECT_EQ(buffer.GetBufferedCount(), 1u);

	now = 100;						  // advance past timeout
	append(h.Feed(2, MakeVideo(3)));  // triggers sweep: 2 flushed (loss of 1), then 3 cascades

	EXPECT_EQ(delivered, (std::vector<int>{0, 1, 2}));
	EXPECT_EQ(buffer.GetBufferedCount(), 0u);
}

TEST(MpegTsDatagramReorderBuffer, DuplicateDatagramPassesThrough)
{
	mpegts::DatagramReorderBuffer buffer;
	Harness h(&buffer);

	std::vector<int> delivered;
	auto append = [&delivered](const std::vector<int> &v) { delivered.insert(delivered.end(), v.begin(), v.end()); };

	append(h.Feed(0, MakeVideo(0)));
	append(h.Feed(1, MakeVideo(1)));
	// A stale/duplicate datagram (behind expected) is indistinguishable from a far-ahead one with a
	// 4-bit counter, so it is not dropped here - it is passed through, and the downstream continuity
	// check handles it. The important property is that nothing received is silently discarded.
	append(h.Feed(2, MakeVideo(0)));

	EXPECT_EQ(delivered, (std::vector<int>{0, 1, 2}));
	EXPECT_EQ(buffer.GetBufferedCount(), 0u);
}

TEST(MpegTsDatagramReorderBuffer, DuplicateInAheadWindowIsNeverDropped)
{
	// A duplicate of an already-buffered (ahead) datagram with a single high-rate PID (no low-rate
	// anchor to reveal it as stale) is not deduped in the buffer - deduping on a counter match could
	// drop genuinely different multi-packet data. Once the gap fills, the original is delivered in
	// order and the surplus copy becomes 'behind' and is passed through (never dropped). The
	// downstream continuity check absorbs the redundant copy.
	mpegts::DatagramReorderBuffer buffer;
	Harness h(&buffer);

	std::vector<int> delivered;
	auto append = [&delivered](const std::vector<int> &v) { delivered.insert(delivered.end(), v.begin(), v.end()); };

	append(h.Feed(0, MakeVideo(0)));   // delivered, expected -> 1
	append(h.Feed(2, MakeVideo(2)));   // ahead -> buffered
	append(h.Feed(99, MakeVideo(2)));  // surplus copy of cc2 -> also buffered
	append(h.Feed(1, MakeVideo(1)));   // fills the gap -> 1, cascade 2, surplus copy passed through

	std::sort(delivered.begin(), delivered.end());
	EXPECT_EQ(delivered, (std::vector<int>{0, 1, 2, 99}));	// every received datagram emitted; none dropped
	EXPECT_EQ(buffer.GetBufferedCount(), 0u);
}

TEST(MpegTsDatagramReorderBuffer, StaleDuplicateRejectedByLowRatePid)
{
	// The joint multi-PID check rejects a stale/duplicate datagram that a high-rate PID alone would
	// alias as "ahead". With 6 video + 1 audio per datagram, video steps by 6 (wraps in ~3 datagrams)
	// but audio steps by 1, so a duplicate of a 2-back datagram shows video "ahead" yet audio
	// "behind" -> classified stale -> passed straight through (never buffered, never re-injected).
	auto run = [](uint8_t v_base, uint8_t a_cc) {
		std::vector<std::pair<uint16_t, uint8_t>> packets;
		for (int i = 0; i < 6; i++)
		{
			packets.push_back({VIDEO_PID, static_cast<uint8_t>((v_base + i) & 0x0F)});
		}
		packets.push_back({AUDIO_PID, a_cc});
		return MakeDatagram(packets);
	};

	mpegts::DatagramReorderBuffer buffer;
	Harness h(&buffer);

	std::vector<int> delivered;
	auto append = [&delivered](const std::vector<int> &v) { delivered.insert(delivered.end(), v.begin(), v.end()); };

	append(h.Feed(0, run(0, 0)));	// video 0-5,  audio 0
	append(h.Feed(1, run(6, 1)));	// video 6-11, audio 1
	append(h.Feed(2, run(12, 2)));	// video 12-15,0,1, audio 2

	// Duplicate of datagram 1 (video first_cc 6, audio 1). video displacement aliases into "ahead",
	// audio displacement is clearly "behind" -> stale -> passthrough, not buffered.
	append(h.Feed(99, run(6, 1)));

	EXPECT_EQ(delivered, (std::vector<int>{0, 1, 2, 99}));	// duplicate passed through, in place
	EXPECT_EQ(buffer.GetBufferedCount(), 0u);
}

TEST(MpegTsDatagramReorderBuffer, PcrOnlyDatagramPreservesReorderBaseline)
{
	// An aligned datagram with no payload PID (e.g. PCR-only / null padding) must pass through
	// without tearing down the sequencing state, so reordering keeps working around it.
	mpegts::DatagramReorderBuffer buffer;
	Harness h(&buffer);

	std::vector<int> delivered;
	auto append = [&delivered](const std::vector<int> &v) { delivered.insert(delivered.end(), v.begin(), v.end()); };

	append(h.Feed(0, MakeVideo(0)));  // primary = video, delivered, expected -> 1
	append(h.Feed(1, MakeVideo(2)));  // ahead -> buffered

	// A PCR-only datagram: one adaptation-field-only packet (AFC=10, no payload) on a PCR PID.
	std::vector<uint8_t> pcr(188, 0x00);
	pcr[0] = 0x47;
	pcr[1] = (0x0100 >> 8) & 0x1F;
	pcr[2] = 0x00;
	pcr[3] = static_cast<uint8_t>((0b10 << 4) | 0);							// adaptation only, no payload
	pcr[4] = 1;																// adaptation_field_length
	append(h.Feed(2, std::make_shared<ov::Data>(pcr.data(), pcr.size())));	// passthrough, no state change

	append(h.Feed(3, MakeVideo(1)));  // still fills the video gap -> 1 then cascade the buffered 2

	EXPECT_EQ(delivered, (std::vector<int>{0, 2, 3, 1}));
	EXPECT_EQ(buffer.GetBufferedCount(), 0u);
}

TEST(MpegTsDatagramReorderBuffer, DiscontinuityBypassesAndRebases)
{
	mpegts::DatagramReorderBuffer buffer;
	Harness h(&buffer);

	std::vector<int> delivered;
	auto append = [&delivered](const std::vector<int> &v) { delivered.insert(delivered.end(), v.begin(), v.end()); };

	append(h.Feed(0, MakeVideo(0)));
	append(h.Feed(1, MakeVideo(1)));
	// A signalled discontinuity with a far-off cc: passes through immediately and rebases.
	append(h.Feed(2, MakeDatagram({{VIDEO_PID, 9}}, /*discontinuity=*/true)));
	// Following datagrams reorder against the new base (cc 10, 11).
	append(h.Feed(3, MakeDatagram({{VIDEO_PID, 11}})));	 // ahead -> buffered
	append(h.Feed(4, MakeDatagram({{VIDEO_PID, 10}})));	 // fills -> 4 then cascade 3

	EXPECT_EQ(delivered, (std::vector<int>{0, 1, 2, 4, 3}));
	EXPECT_EQ(buffer.GetBufferedCount(), 0u);
}

TEST(MpegTsDatagramReorderBuffer, UnalignedDatagramPassesThrough)
{
	mpegts::DatagramReorderBuffer buffer;
	Harness h(&buffer);

	// Not a multiple of 188 -> cannot be trusted for CC ordering -> passed through as-is.
	std::vector<uint8_t> junk(200, 0x00);
	auto datagram = std::make_shared<ov::Data>(junk.data(), junk.size());

	EXPECT_EQ(h.Feed(0, datagram), (std::vector<int>{0}));
	EXPECT_EQ(buffer.GetBufferedCount(), 0u);
}

TEST(MpegTsDatagramReorderBuffer, MultiPidInterleaveIsRestored)
{
	mpegts::DatagramReorderBuffer buffer;
	Harness h(&buffer);

	std::vector<int> delivered;
	auto append = [&delivered](const std::vector<int> &v) { delivered.insert(delivered.end(), v.begin(), v.end()); };

	// Each datagram carries video and audio packets that move together.
	auto d0		= MakeDatagram({{VIDEO_PID, 0}, {AUDIO_PID, 5}});
	auto d1		= MakeDatagram({{VIDEO_PID, 1}, {AUDIO_PID, 6}});
	auto d2		= MakeDatagram({{VIDEO_PID, 2}, {AUDIO_PID, 7}});

	append(h.Feed(0, d0));
	append(h.Feed(2, d2));	// ahead -> buffered
	append(h.Feed(1, d1));	// fills -> 1 then cascade 2

	EXPECT_EQ(delivered, (std::vector<int>{0, 1, 2}));
	EXPECT_EQ(buffer.GetBufferedCount(), 0u);
}

TEST(MpegTsDatagramReorderBuffer, NonPrimaryPidDatagramPassesThrough)
{
	// The primary (sequencing) PID is the first payload PID seen (video here). A datagram that does
	// not carry it (a separate audio-only datagram) is not reordered; it passes straight through in
	// arrival order while the primary PID is reordered independently.
	mpegts::DatagramReorderBuffer buffer;
	Harness h(&buffer);

	std::vector<int> delivered;
	auto append = [&delivered](const std::vector<int> &v) { delivered.insert(delivered.end(), v.begin(), v.end()); };

	append(h.Feed(0, MakeVideo(0)));					// primary = video, delivered, expected -> 1
	append(h.Feed(1, MakeVideo(2)));					// video ahead -> buffered
	append(h.Feed(2, MakeDatagram({{AUDIO_PID, 7}})));	// audio-only, no primary -> passthrough
	append(h.Feed(3, MakeVideo(1)));					// fills the video gap -> 1 then cascade 2

	EXPECT_EQ(delivered, (std::vector<int>{0, 2, 3, 1}));
	EXPECT_EQ(buffer.GetBufferedCount(), 0u);
}

TEST(MpegTsDatagramReorderBuffer, FarAheadDatagramPassesThrough)
{
	// Documents the 4-bit continuity-counter limit: a datagram displaced beyond the ahead window is
	// ambiguous (far-ahead vs stale). It is passed through rather than restored or dropped, so no
	// received data is lost even though order is not recovered.
	mpegts::DatagramReorderBuffer buffer;
	Harness h(&buffer);

	h.Feed(0, MakeVideo(0));			  // expected -> 1
	auto out = h.Feed(1, MakeVideo(10));  // displacement 9 (> ahead limit) -> passed through

	EXPECT_EQ(out, (std::vector<int>{1}));
	EXPECT_EQ(buffer.GetBufferedCount(), 0u);
}

TEST(MpegTsDatagramReorderBuffer, AdjacentSwapRestoredWithMultiPacketDatagrams)
{
	// Realistic framing: 7 TS packets per datagram (the counter steps by 7 each datagram). An
	// adjacent datagram swap is still restored.
	mpegts::DatagramReorderBuffer buffer;
	Harness h(&buffer);

	std::vector<int> delivered;
	auto append = [&delivered](const std::vector<int> &v) { delivered.insert(delivered.end(), v.begin(), v.end()); };

	auto d0		= MakeVideoRun(0, 7);	// cc 0..6
	auto d1		= MakeVideoRun(7, 7);	// cc 7..13
	auto d2		= MakeVideoRun(14, 7);	// cc 14,15,0,1,2,3,4
	auto d3		= MakeVideoRun(5, 7);	// cc 5..11

	append(h.Feed(0, d0));
	append(h.Feed(2, d2));	// d1/d2 swapped in arrival
	append(h.Feed(1, d1));
	append(h.Feed(3, d3));

	EXPECT_EQ(delivered, (std::vector<int>{0, 1, 2, 3}));
	EXPECT_EQ(buffer.GetBufferedCount(), 0u);
}

TEST(MpegTsDatagramReorderBuffer, DeepReorderPassesThroughWithoutLossMultiPacket)
{
	// With 7-packet datagrams a depth-2 reorder exceeds the 4-bit counter window and cannot be
	// restored, but the key guarantee holds: every received datagram is still delivered (nothing is
	// dropped), even if the order is not fully recovered.
	mpegts::DatagramReorderBuffer buffer;
	Harness h(&buffer);

	std::vector<int> delivered;
	auto append = [&delivered](const std::vector<int> &v) { delivered.insert(delivered.end(), v.begin(), v.end()); };

	auto d0		= MakeVideoRun(0, 7);
	auto d1		= MakeVideoRun(7, 7);
	auto d2		= MakeVideoRun(14, 7);
	auto d3		= MakeVideoRun(5, 7);

	append(h.Feed(0, d0));
	append(h.Feed(2, d2));
	append(h.Feed(3, d3));
	append(h.Feed(1, d1));
	append(h.FlushRemaining());

	std::sort(delivered.begin(), delivered.end());
	EXPECT_EQ(delivered, (std::vector<int>{0, 1, 2, 3}));
	EXPECT_EQ(buffer.GetBufferedCount(), 0u);
}

TEST(MpegTsDatagramReorderBuffer, BypassDrainsBufferedInRestoredOrder)
{
	mpegts::DatagramReorderBuffer buffer;
	Harness h(&buffer);

	std::vector<int> delivered;
	auto append = [&delivered](const std::vector<int> &v) { delivered.insert(delivered.end(), v.begin(), v.end()); };

	append(h.Feed(0, MakeVideo(0)));  // delivered, expected -> 1
	append(h.Feed(3, MakeVideo(3)));  // buffered (arrival first)
	append(h.Feed(2, MakeVideo(2)));  // buffered (arrival second)

	// An unaligned datagram forces the buffer to drain. It must drain in restored order (2 then 3),
	// not arrival order (3 then 2), before the passthrough datagram.
	std::vector<uint8_t> junk(200, 0x00);
	append(h.Feed(9, std::make_shared<ov::Data>(junk.data(), junk.size())));

	EXPECT_EQ(delivered, (std::vector<int>{0, 2, 3, 9}));
	EXPECT_EQ(buffer.GetBufferedCount(), 0u);
}

TEST(MpegTsDatagramReorderBuffer, CorruptAdaptationLengthBypasses)
{
	mpegts::DatagramReorderBuffer buffer;
	Harness h(&buffer);

	// AFC=11 (adaptation + payload) with an adaptation_field_length that exceeds the packet body.
	std::vector<uint8_t> bytes(188, 0x00);
	bytes[0]	  = 0x47;
	bytes[1]	  = (VIDEO_PID >> 8) & 0x1F;
	bytes[2]	  = VIDEO_PID & 0xFF;
	bytes[3]	  = static_cast<uint8_t>((0b11 << 4) | 0);
	bytes[4]	  = 200;  // invalid adaptation_field_length (> 183)
	auto datagram = std::make_shared<ov::Data>(bytes.data(), bytes.size());

	// Untrusted -> bypass reordering -> passed through unchanged.
	EXPECT_EQ(h.Feed(0, datagram), (std::vector<int>{0}));
	EXPECT_EQ(buffer.GetBufferedCount(), 0u);
}

TEST(MpegTsDatagramReorderBuffer, MultiPacketFlushNeverDropsReceivedDatagram)
{
	// Regression: with multi-packet datagrams the 4-bit counter can make a timeout/depth flush pick a
	// victim that advances the baseline past a still-buffered datagram. That datagram must be passed
	// through, never silently dropped.
	int64_t now = 0;
	mpegts::DatagramReorderBuffer buffer(/*max_datagrams=*/8, /*timeout_msec=*/100,
										 [&now]() { return now; });
	Harness h(&buffer);

	std::vector<int> delivered;
	auto append = [&delivered](const std::vector<int> &v) { delivered.insert(delivered.end(), v.begin(), v.end()); };

	append(h.Feed(2, MakeVideoRun(6, 3)));	 // first delivered, expected -> 9
	append(h.Feed(0, MakeVideoRun(0, 3)));	 // displacement 7 -> buffered
	now = 100;								 // age past timeout
	append(h.Feed(5, MakeVideoRun(15, 3)));	 // displacement 6 -> buffered; timeout flush fires
	append(h.FlushRemaining());

	std::sort(delivered.begin(), delivered.end());
	EXPECT_EQ(delivered, (std::vector<int>{0, 2, 5}));	// every received datagram emitted; none dropped
	EXPECT_EQ(buffer.GetBufferedCount(), 0u);
}

// -----------------------------------------------------------------------------
// Property test: within-window shuffles must restore the original order exactly.
// Uses only the raw mt19937 engine output (no std distributions/std::shuffle) so
// the permutation is byte-for-byte reproducible on every platform and compiler.
// -----------------------------------------------------------------------------
TEST(MpegTsDatagramReorderBuffer, WithinWindowShufflePropertyRestoresOrder)
{
	constexpr int BLOCK = 6;   // reorder distance bound (<= ahead limit)
	constexpr int COUNT = 50;  // crosses several 4-bit continuity-counter wraps

	for (uint32_t seed = 1; seed <= 25; seed++)
	{
		std::mt19937 gen(seed);

		// Build a bounded-displacement permutation using only raw gen() outputs (Fisher-Yates within
		// fixed blocks). Every datagram stays inside its block, so its displacement is < BLOCK and
		// the continuity counters live in the buffer never span a full 4-bit wrap (no CC aliasing).
		// Index 0 is kept first so it establishes the reorder baseline (in a real stream the
		// first-arriving datagram is the baseline; there is no datagram "before" it).
		std::vector<int> feed_order(COUNT);
		for (int i = 0; i < COUNT; i++)
		{
			feed_order[i] = i;
		}
		for (int start = 1; start < COUNT; start += BLOCK)
		{
			const int end = std::min(start + BLOCK, COUNT);
			for (int i = end - 1; i > start; i--)
			{
				const int j = start + static_cast<int>(gen() % static_cast<uint32_t>(i - start + 1));
				std::swap(feed_order[i], feed_order[j]);
			}
		}

		// No loss and no time advance -> full restoration is required.
		mpegts::DatagramReorderBuffer buffer(/*max_datagrams=*/64, /*timeout_msec=*/1000000,
											 /*clock=*/[]() { return static_cast<int64_t>(0); });
		Harness h(&buffer);

		std::vector<int> delivered;
		for (int index : feed_order)
		{
			auto out = h.Feed(index, MakeVideo(static_cast<uint8_t>(index % 16)));
			delivered.insert(delivered.end(), out.begin(), out.end());
		}
		auto tail = h.FlushRemaining();
		delivered.insert(delivered.end(), tail.begin(), tail.end());

		std::vector<int> expected(COUNT);
		for (int i = 0; i < COUNT; i++)
		{
			expected[i] = i;
		}

		ASSERT_EQ(delivered, expected) << "seed=" << seed;
	}
}
