//==============================================================================
//
//  OvenMediaEngine
//
//==============================================================================
#pragma once

#include <base/ovlibrary/ovlibrary.h>

#include <chrono>
#include <deque>
#include <mutex>

// Stream-scoped adaptive smoothing-delay controller.
//
// Each FramePacer records its observed lateness (arrival vs. PTS-anchored
// expected time) into this shared controller. The controller maintains a
// rolling window of samples across all tracks and exposes a single current
// delay so that all tracks of a stream stay in lock-step (preserves A/V sync).
//
// The current delay tracks a target percentile of recent lateness plus a small
// margin: the Pacer absorbs everything up to that percentile, and the
// remaining tail passes through to the receiver where the player's own jitter
// buffer covers it. This keeps sender-side latency tight while still spreading
// frames evenly for the common case.
//
// Setting min == max effectively pins the delay to a fixed value.
class AdaptiveDelayController
{
public:
	AdaptiveDelayController(int min_delay_ms, int max_delay_ms);

	// Record a per-frame lateness sample (in ms; can be negative).
	void RecordSample(uint32_t track_id, int64_t lateness_ms);

	// Current smoothing delay to apply (ms). Cheap to call per-frame.
	int GetCurrentDelayMs();

private:
	void Recompute();

	struct Sample
	{
		std::chrono::steady_clock::time_point ts;
		int64_t lateness_ms;
		uint32_t track_id;
	};

	int _min_delay_ms;
	int _max_delay_ms;

	std::mutex _mu;
	std::deque<Sample> _samples;
	int _current_delay_ms;
	std::chrono::steady_clock::time_point _last_recompute;

	// Warning state (rate-limited).
	std::chrono::steady_clock::time_point _last_lateness_warn;
	std::chrono::steady_clock::time_point _last_max_warn;
	bool _at_max = false;

	// Periodic measurement log (for tuning Min/Max defaults).
	std::chrono::steady_clock::time_point _last_stats_log;
	void LogPerTrackStats();
};
