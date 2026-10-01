#pragma once

#include <base/ovlibrary/ovlibrary.h>
#include <chrono>
#include <deque>
#include <map>
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
	// Floor of the absolute age cap for a pending seq the jitter buffer never
	// advances past (e.g. very first packet of a stream lost before any frame
	// is built). The effective cap is at least max_hold_ms so retries outlive
	// the buffer's hold; in the normal path DropPendingUpTo ends entries first.
	static constexpr uint32_t MAX_AGE_MS	= 500;
	// Number of round trips the hold window reserves. 5 is conservative
	// against burst loss where independent-probability math doesn't apply.
	static constexpr uint32_t MAX_NACK_RETRIES = 5;
	// Dwell time between gap detection and the initial NACK firing.
	// Absorbs small UDP reordering so that brief out-of-order delivery
	// (seq 102 before 101) doesn't trigger a spurious NACK + RTX round-trip.
	static constexpr uint32_t INITIAL_NACK_DWELL_MS = 10;

	static constexpr uint32_t HOLD_MIN_MS		= 50;
	// Bounds the one-off stall when a frame never recovers. 600 keeps two
	// retransmission attempts inside the window up to a ~250ms round trip.
	static constexpr uint32_t HOLD_MAX_MS_DEFAULT = 600;
	// Initial RTT guess, held in the sample window until real samples push it
	// out. Err high: an overestimate only lengthens the give-up wait on frames
	// that never recover, while an underestimate discards recoverable ones.
	static constexpr double INITIAL_RTT_GUESS_MS = 100.0;
	// The RTT estimate is the max of this many recent samples. A max reacts to
	// a slower path on the first clean sample and is immune to a stray sample
	// that arrived faster than any real answer could.
	static constexpr size_t RTT_WINDOW_SIZE = 8;
	// Margin added to the RTT for the retry interval. Retries are quantized by
	// the caller's flush cadence, so a smaller margin would fire spurious
	// retries just ahead of an on-time RTX.
	static constexpr uint32_t MIN_RETRY_MARGIN_MS = 20;
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
	//   hold = dwell + MAX_NACK_RETRIES * rtt * backoff
	// clamped to [HOLD_MIN_MS, max_hold_ms], where rtt is the max of the
	// recent sample window (seeded with INITIAL_RTT_GUESS_MS) and backoff is
	// the retry multiplier, so a frame stays long enough for the answer a
	// backed-off retry waits for. After STATS_DECAY_MS without a new sample
	// rtt is raised to at least the guess.
	uint32_t GetRecommendedHoldMs() const;

private:
	struct PendingEntry
	{
		std::chrono::steady_clock::time_point first_nack_at;
		std::chrono::steady_clock::time_point last_nack_at;
		uint32_t retry_count = 0;
		std::chrono::steady_clock::time_point inserted_at;
	};

	std::optional<uint32_t> ExtendSeq(uint16_t seq) const OV_REQUIRES(_lock);
	void UpdateLatencyStats(double sample_ms, std::chrono::steady_clock::time_point now) OV_REQUIRES(_lock);
	void DiscardStale(std::chrono::steady_clock::time_point now) OV_REQUIRES(_lock);
	void LogPeriodicStats(std::chrono::steady_clock::time_point now) OV_REQUIRES(_lock);
	// Hold recommendation core; assumes _lock is already held.
	uint32_t GetRecommendedHoldMsInternal() const OV_REQUIRES(_lock);
	// NACK retry interval: (rtt + MIN_RETRY_MARGIN_MS) scaled by the current
	// backoff, capped at max_hold_ms.
	uint32_t GetRetryIntervalMsInternal() const OV_REQUIRES(_lock);
	// Max of the recent sample window, raised to the guess after a drought.
	double CurrentRttMsInternal() const OV_REQUIRES(_lock);

	uint32_t _track_id = 0;
	uint32_t _media_ssrc = 0;
	uint32_t _hold_max_ms = HOLD_MAX_MS_DEFAULT;

	bool _initialized OV_GUARDED_BY(_lock) = false;
	uint32_t _newest_extended OV_GUARDED_BY(_lock) = 0;	// last seq seen in extended (uint32) form
	uint32_t _expected_next OV_GUARDED_BY(_lock) = 0;	// next extended seq we expect

	std::map<uint32_t /*extended seq*/, PendingEntry> _pending OV_GUARDED_BY(_lock);

	// Recent NACK->RTX round-trip samples in milliseconds, newest last. Only
	// single-NACK recoveries are sampled (Karn). The initial guess sits in
	// the window as a sample, so the estimate starts conservative and hands
	// over to real samples only once enough of them landed.
	std::deque<double> _rtt_window_ms OV_GUARDED_BY(_lock) = {INITIAL_RTT_GUESS_MS};
	bool _has_rtt_sample OV_GUARDED_BY(_lock) = false;
	std::chrono::steady_clock::time_point _last_sample_at OV_GUARDED_BY(_lock);

	// Retry backoff multiplier. Doubles on every retry round and resets on a
	// clean sample, so the retry interval and the hold outgrow an RTT the
	// estimator could not measure yet (Karn's rule alone would never sample it).
	uint32_t _retry_backoff OV_GUARDED_BY(_lock) = 1;

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
