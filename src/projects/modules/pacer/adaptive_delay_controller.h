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
// Setting min == max effectively pins the delay to a fixed value.
class AdaptiveDelayController
{
public:
	AdaptiveDelayController(int min_delay_ms, int max_delay_ms);

	// Record a per-frame lateness sample (in ms; can be negative).
	void RecordSample(int64_t lateness_ms);

	// Current smoothing delay to apply (ms). Cheap to call per-frame.
	int GetCurrentDelayMs();

private:
	void Recompute();

	struct Sample
	{
		std::chrono::steady_clock::time_point ts;
		int64_t lateness_ms;
	};

	int _min_delay_ms;
	int _max_delay_ms;

	std::mutex _mu;
	std::deque<Sample> _samples;
	int _current_delay_ms;
	std::chrono::steady_clock::time_point _last_recompute;

	// Warning state (rate-limited).
	std::chrono::steady_clock::time_point _last_lateness_warn;
	std::chrono::steady_clock::time_point _last_fastup_warn;
	std::chrono::steady_clock::time_point _last_max_warn;
	bool _at_max = false;
};
