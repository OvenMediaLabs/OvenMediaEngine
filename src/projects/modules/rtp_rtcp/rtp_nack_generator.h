#pragma once

#include <base/ovlibrary/ovlibrary.h>
#include <chrono>
#include <map>
#include <mutex>
#include <optional>
#include <vector>

// Per-track receive-side NACK generator (RFC 4585 Generic NACK).
//
// Watches incoming RTP sequence numbers for one SSRC, builds the list of
// seqs that should be NACK'd next, and tracks NACK->RTX round-trip latency
// to recommend a jitter-buffer hold window.
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
	// Absolute safety cap when the jitter buffer never advances past a seq
	// (e.g. very first packet of a stream lost before any frame is built).
	// In the normal path, the jitter buffer's DropPendingUpTo callback ends
	// pending entries far sooner than this.
	static constexpr uint32_t MAX_AGE_MS	= 500;
	// Number of NACK retries the hold window must accommodate. The hold
	// formula reserves room for this many retry intervals plus one final
	// RTT for the last RTX response. 5 is conservative against burst loss
	// where independent-probability math doesn't apply.
	static constexpr uint32_t MAX_NACK_RETRIES = 5;
	// Dwell time between gap detection and the initial NACK firing.
	// Absorbs small UDP reordering so that brief out-of-order delivery
	// (seq 102 before 101) doesn't trigger a spurious NACK + RTX round-trip.
	static constexpr uint32_t INITIAL_NACK_DWELL_MS = 10;

	static constexpr uint32_t HOLD_MIN_MS		= 50;
	static constexpr uint32_t HOLD_MAX_MS_DEFAULT = 400;
	// Initial RTT guess used as both the seed for the retry interval and
	// the substitute EWMA value while no NACK->RTX sample has been recorded
	// (or after long stats decay). Real value lands within a few hundred ms.
	static constexpr double INITIAL_RTT_GUESS_MS = 15.0;
	static constexpr double EWMA_ALPHA			= 0.125;
	static constexpr double EWMA_DEV_ALPHA		= 0.25;
	static constexpr double EWMA_DEV_MULTIPLIER	= 4.0;
	static constexpr uint32_t STATS_DECAY_MS	= 30 * 1000;
	static constexpr uint32_t STATS_LOG_INTERVAL_MS = 5 * 1000;

	RtpNackGenerator(uint32_t track_id, uint32_t media_ssrc, uint32_t max_hold_ms = HOLD_MAX_MS_DEFAULT);

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

	// Jitter-buffer hold window recommendation in ms.
	//   hold = dwell + MAX_NACK_RETRIES * ewma + 4 * dev
	// clamped to [HOLD_MIN_MS, max_hold_ms]. Each round (NACK + RTX answer)
	// takes one ewma, so N rounds complete in N * ewma. Before the first
	// NACK->RTX sample (or after STATS_DECAY_MS of no new sample), ewma
	// falls back to INITIAL_RTT_GUESS_MS and dev to 0.
	uint32_t GetRecommendedHoldMs() const;

private:
	struct PendingEntry
	{
		std::chrono::steady_clock::time_point first_nack_at;
		std::chrono::steady_clock::time_point last_nack_at;
		uint32_t retry_count = 0;
		std::chrono::steady_clock::time_point inserted_at;
	};

	std::optional<uint32_t> ExtendSeq(uint16_t seq) const;
	void UpdateLatencyStats(double sample_ms, std::chrono::steady_clock::time_point now);
	void DiscardStale(std::chrono::steady_clock::time_point now);
	void LogPeriodicStats(std::chrono::steady_clock::time_point now);
	// Hold recommendation core; assumes _lock is already held.
	uint32_t GetRecommendedHoldMsInternal() const;

	uint32_t _track_id = 0;
	uint32_t _media_ssrc = 0;
	uint32_t _hold_max_ms = HOLD_MAX_MS_DEFAULT;

	bool _initialized = false;
	uint32_t _newest_extended = 0;	// last seq seen in extended (uint32) form
	uint32_t _expected_next = 0;	// next extended seq we expect

	std::map<uint32_t /*extended seq*/, PendingEntry> _pending;

	// NACK->RTX latency stats (smoothed mean + mean-deviation, milliseconds).
	// _ewma_ms doubles as the NACK retry interval. Seeded with the same
	// initial RTT guess that GetRecommendedHoldMs falls back to, so retry
	// timing and hold timing agree before the first sample lands.
	bool _stats_initialized = false;
	double _ewma_ms = INITIAL_RTT_GUESS_MS;
	double _ewma_dev_ms = 0.0;
	std::chrono::steady_clock::time_point _last_sample_at;

	// Cumulative monitoring counters. Logged every STATS_LOG_INTERVAL_MS as
	// deltas (loss / recovery snapshot) and as cumulative totals.
	uint64_t _received_total = 0;
	uint64_t _nacks_sent_total = 0;
	uint64_t _recovered_total = 0;
	uint64_t _lost_permanent_total = 0;
	uint64_t _prev_received = 0;
	uint64_t _prev_nacks_sent = 0;
	uint64_t _prev_recovered = 0;
	uint64_t _prev_lost_permanent = 0;
	std::chrono::steady_clock::time_point _last_stats_log_at;

	mutable std::mutex _lock;
};
