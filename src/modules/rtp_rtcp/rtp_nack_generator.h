#pragma once

#include <base/ovlibrary/ovlibrary.h>
#include <chrono>
#include <map>
#include <optional>
#include <vector>

// Per-track receive-side NACK generator (RFC 4585 Generic NACK).
//
// Watches incoming RTP sequence numbers for one SSRC and builds the list of
// seqs that should be NACK'd next. A missing seq is re-requested on a fixed
// cadence for as long as the jitter buffer holds its frame. The sender paces
// its own retransmissions per round trip, so the receiver does not need to
// know the RTT.
//
// Caller (RtpRtcp) drives:
//   - OnPacketReceived(seq) for every incoming RTP packet of the SSRC,
//     including original payload and RTX-unwrapped packets.
//   - BuildPendingNack() on a short cadence (e.g. 10ms tick) and sends
//     the returned list as one RTCP NACK FCI chain.
class RtpNackGenerator
{
public:
	static constexpr size_t MAX_PENDING		= 250;
	// Floor of the absolute age cap for a pending seq the jitter buffer never
	// advances past (e.g. very first packet of a stream lost before any frame
	// is built). The effective cap is at least hold_ms so retries outlive the
	// buffer's hold; in the normal path DropPendingUpTo ends entries first.
	static constexpr uint32_t MAX_AGE_MS	= 500;
	// Dwell time between gap detection and the initial NACK firing.
	// Absorbs small UDP reordering so that brief out-of-order delivery
	// (seq 102 before 101) doesn't trigger a spurious NACK + RTX round-trip.
	static constexpr uint32_t INITIAL_NACK_DWELL_MS = 10;
	// Re-request cadence for a seq still missing. Senders answer a given seq
	// at most once per round trip, so asking more often than the RTT only
	// costs a small RTCP packet, while asking less often delays the second
	// attempt when the first answer was lost.
	static constexpr uint32_t RETRY_INTERVAL_MS = 100;
	// How long the jitter buffer holds an incomplete frame for retransmissions
	// before discarding it (MaxHoldMs). Bounds the one-off stall when a frame
	// never recovers; 600 fits four answered attempts up to a ~250ms round trip.
	static constexpr uint32_t HOLD_MS_DEFAULT = 600;
	static constexpr uint32_t STATS_LOG_INTERVAL_MS = 5 * 1000;

	RtpNackGenerator(uint32_t track_id, uint32_t media_ssrc, uint32_t hold_ms = HOLD_MS_DEFAULT);

	uint32_t GetMediaSsrc() const { return _media_ssrc; }

	// Feed every received RTP packet's sequence number, in arrival order.
	void OnPacketReceived(uint16_t seq);

	// Returns seqs to send in the next NACK packet (initial + due retries).
	// Caller MUST send them; this call mutates retry state and advances
	// last-request timestamps. Returned list is in ascending seq order
	// suitable for NACK::AddLostId() FCI grouping.
	std::vector<uint16_t> BuildPendingNack();

	// Drop pending entries whose seq <= max_seq (wrap-safe). Called by the
	// jitter buffer when it advances past a frame so we stop chasing seqs
	// the consumer no longer wants.
	void DropPendingUpTo(uint16_t max_seq);

	// Lowest seq still pending NACK recovery, if any. The jitter buffer
	// uses this to hold a complete frame whose first packet is newer than
	// a pending recovery, so we don't emit out-of-order before NACK has a
	// chance.
	std::optional<uint16_t> GetLowestPendingSeq() const;

	// Jitter-buffer hold window in ms: the configured MaxHoldMs.
	uint32_t GetHoldMs() const { return _hold_ms; }

private:
	struct PendingEntry
	{
		std::chrono::steady_clock::time_point first_nack_at;
		std::chrono::steady_clock::time_point last_nack_at;
		uint32_t retry_count = 0;
		std::chrono::steady_clock::time_point inserted_at;
	};

	std::optional<uint32_t> ExtendSeq(uint16_t seq) const OV_REQUIRES(_lock);
	void DiscardStale(std::chrono::steady_clock::time_point now) OV_REQUIRES(_lock);
	void LogPeriodicStats(std::chrono::steady_clock::time_point now) OV_REQUIRES(_lock);

	uint32_t _track_id = 0;
	uint32_t _media_ssrc = 0;
	uint32_t _hold_ms = HOLD_MS_DEFAULT;

	bool _initialized OV_GUARDED_BY(_lock) = false;
	uint32_t _newest_extended OV_GUARDED_BY(_lock) = 0;	// last seq seen in extended (uint32) form
	uint32_t _expected_next OV_GUARDED_BY(_lock) = 0;	// next extended seq we expect

	std::map<uint32_t /*extended seq*/, PendingEntry> _pending OV_GUARDED_BY(_lock);

	// Cumulative monitoring counters. Logged every STATS_LOG_INTERVAL_MS as
	// deltas (loss / recovery snapshot) and as cumulative totals.
	uint64_t _received_total OV_GUARDED_BY(_lock) = 0;
	uint64_t _nacks_sent_total OV_GUARDED_BY(_lock) = 0;
	uint64_t _recovered_total OV_GUARDED_BY(_lock) = 0;
	uint64_t _lost_permanent_total OV_GUARDED_BY(_lock) = 0;
	uint64_t _prev_received OV_GUARDED_BY(_lock) = 0;
	uint64_t _prev_nacks_sent OV_GUARDED_BY(_lock) = 0;
	uint64_t _prev_recovered OV_GUARDED_BY(_lock) = 0;
	uint64_t _prev_lost_permanent OV_GUARDED_BY(_lock) = 0;
	std::chrono::steady_clock::time_point _last_stats_log_at OV_GUARDED_BY(_lock);

	mutable ov::Mutex _lock;
};
