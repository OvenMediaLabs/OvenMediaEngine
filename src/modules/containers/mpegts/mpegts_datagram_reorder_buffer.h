//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2026 OvenMediaLabs. All rights reserved.
//
//==============================================================================
#pragma once

#include <base/ovlibrary/ovlibrary.h>

#include <deque>
#include <functional>
#include <map>
#include <vector>

namespace mpegts
{
	// Reorders whole UDP datagrams (each a contiguous run of 188-byte TS packets)
	// that a lossy network delivered out of order, using the joint per-PID continuity counters
	// as the sequencing hint.
	//
	// UDP reorders datagrams atomically (the TS packets inside one datagram are always in send order).
	// A datagram carries several PIDs, each with its own continuity counter;
	// the buffer keeps the expected next counter per PID and classifies an incoming datagram
	// by ALL the PIDs it carries at once:
	//
	//   - every carried (already-tracked) PID continues exactly  -> in order, deliver
	//   - every carried PID is ahead of expected                 -> a gap; buffer until it fills
	//   - any carried PID is behind expected                     -> stale/duplicate; pass through
	//
	// The joint check is what makes this robust. The counter is only 4 bits, so a high-rate PID
	// (video: several packets per datagram) wraps every ~2-3 datagrams and cannot alone tell
	// "a few datagrams ahead" from "a few datagrams behind".
	// A low-rate PID (audio/PCR: ~1 packet per datagram) advances its counter by ~1 per datagram,
	// so it effectively numbers datagrams with a ~16-datagram horizon and reliably reveals a behind/stale datagram even
	// when the high-rate PID aliases it as "ahead".
	// Requiring all PIDs to agree therefore rejects stale/duplicate datagrams
	// (they are passed straight through, never buffered) and admits only genuine gaps.
	//
	// A gap that never fills is declared lost after a bounded window (depth) or timeout.
	// A datagram that cannot be placed (ambiguous, or beyond the window) is passed through, never dropped,
	// so no received data is lost; downstream continuity checks turn any residual disorder into a clean gap.
	// Reordering is a best-effort improvement, never worse than not reordering at all.
	// A datagram carrying only a high-rate PID (a pure video burst with no low-rate anchor) can still be
	// misjudged within that burst, but the pass-through-never-drop rule keeps that safe.
	//
	// Not thread-safe: the owning depacketizer serializes all access under its own lock.
	class DatagramReorderBuffer
	{
	public:
		// Maximum datagrams held before the head gap is declared lost and flushed.
		static constexpr size_t DEFAULT_MAX_DATAGRAMS = 8;

		// Maximum time a datagram waits behind a gap before the gap is declared lost (milliseconds).
		static constexpr int64_t DEFAULT_TIMEOUT_MSEC = 100;

		// Continuity-counter displacement (mod 16) at or below which a PID is treated as "ahead"
		// (a plausible future) rather than "behind" (already delivered/stale).
		// A loss larger than this window puts the stream beyond it, so reordering suspends
		// (datagrams pass straight through, none dropped) until a signalled discontinuity
		// or the counter naturally re-enters the window.
		static constexpr uint8_t AHEAD_LIMIT		  = 7;

		// `clock` returns the current time in milliseconds; `nullptr` uses the system clock.
		// Injecting a clock keeps timeout-driven behavior deterministic in tests.
		explicit DatagramReorderBuffer(size_t max_datagrams			  = DEFAULT_MAX_DATAGRAMS,
									   int64_t timeout_msec			  = DEFAULT_TIMEOUT_MSEC,
									   std::function<int64_t()> clock = nullptr);

		// Feeds one whole datagram. Appends any datagrams that become deliverable, in restored order, to `out`.
		// Returns the number of datagrams whose preceding gap was declared lost (depth/timeout flush) during this call,
		// so the caller can log that event.
		size_t Enqueue(const std::shared_ptr<const ov::Data> &datagram,
					   std::vector<std::shared_ptr<const ov::Data>> *out);

		// Emits everything still buffered (in restored order). Available for an explicit teardown drain;
		// it is not called automatically on destruction, so if a stream ends while datagrams are held behind a gap,
		// up to (`max_datagrams - 1`) already-received datagrams are dropped at end-of-stream.
		// That is bounded and only affects shutdown.
		void Flush(std::vector<std::shared_ptr<const ov::Data>> *out);

		size_t GetBufferedCount() const
		{
			return _buffer.size();
		}

	private:
		// Per-datagram continuity-counter summary extracted by a light header-only parse.
		struct Summary
		{
			// starts on a 188 boundary and every sync byte is present
			bool aligned	   = false;
			// a packet signals discontinuity_indicator
			bool discontinuity = false;
			// payload-bearing PIDs in packet order
			std::vector<uint16_t> pid_order;
			std::map<uint16_t, uint8_t> first_cc;
			std::map<uint16_t, uint8_t> last_cc;
		};

		struct Pending
		{
			std::shared_ptr<const ov::Data> data;
			Summary summary;
			int64_t arrival_msec = 0;
		};

		enum class Placement
		{
			// no PID is tracked yet (stream start/all-new PIDs)
			Init,
			// every tracked PID continues exactly
			InOrder,
			// every tracked PID is ahead (a gap); none behind
			Ahead,
			// at least one tracked PID is behind (already delivered/duplicate)
			Stale,
		};

		static Summary Parse(const std::shared_ptr<const ov::Data> &datagram);
		static uint8_t Displacement(uint8_t cc, uint8_t expected);

		int64_t Now() const;
		Placement Classify(const Summary &summary) const;

		// Delivers a datagram and advances the expected counter for every PID it carries.
		void Deliver(const std::shared_ptr<const ov::Data> &data, const Summary &summary,
					 std::vector<std::shared_ptr<const ov::Data>> *out);

		void Cascade(std::vector<std::shared_ptr<const ov::Data>> *out);

		// The buffered datagram nearest the expected counters (smallest displacement over its tracked
		// PIDs); the best guess for the datagram right after a lost head gap.
		std::deque<Pending>::iterator SelectClosest();

		// Returns the number of gaps declared lost (datagrams force-flushed) during this call.
		size_t FlushIfNeeded(std::vector<std::shared_ptr<const ov::Data>> *out);

		// Drains the whole buffer in restored (oldest-first) order.
		void DrainInOrder(std::vector<std::shared_ptr<const ov::Data>> *out);

		// Re-seeds the expected counters for the PIDs a datagram carries (used after a discontinuity).
		void Rebase(const Summary &summary);

		size_t _max_datagrams;
		int64_t _timeout_msec;
		std::function<int64_t()> _clock;

		// Expected next continuity counter per PID (the joint sequencing state).
		std::map<uint16_t, uint8_t> _expected_cc;

		std::deque<Pending> _buffer;
	};
}  // namespace mpegts
