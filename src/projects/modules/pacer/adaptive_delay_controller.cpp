//==============================================================================
//
//  OvenMediaEngine
//
//==============================================================================
#include "adaptive_delay_controller.h"

#include <algorithm>
#include <vector>

#define OV_LOG_TAG "AdaptiveDelay"

// Rolling window of samples used to compute the percentile. Larger window =
// more stable estimate but slower to adapt to genuine condition changes.
static constexpr auto kWindow = std::chrono::seconds(30);

// Recompute is throttled to this interval (avoids re-sorting per-frame).
static constexpr auto kRecomputeInterval = std::chrono::milliseconds(500);

// Target percentile of recent lateness. The Pacer absorbs everything up to
// this percentile; the remaining tail (here ~5%) passes straight through to
// the receiver, where the player's own jitter buffer covers it. Trading
// "perfect" pacing for lower sender-side latency.
static constexpr double kTargetPercentile = 0.95;

// Below this many samples, percentile estimate is unreliable; skip recompute
// (current_delay stays at Min until enough samples accumulate).
static constexpr size_t kMinSamplesForRecompute = 30;

// Small headroom added on top of the percentile so frames that fall in the
// (p95, p95 + margin] band are still absorbed and aren't pushed to the tail.
static constexpr int kMarginMs = 10;

// Warning rate limit (per warning type).
static constexpr auto kWarnThrottle = std::chrono::seconds(1);

// Periodic per-track lateness statistics dump (for measurement / tuning).
static constexpr auto kStatsLogInterval = std::chrono::seconds(5);

AdaptiveDelayController::AdaptiveDelayController(int min_delay_ms, int max_delay_ms)
	: _min_delay_ms(min_delay_ms),
	  _max_delay_ms(std::max(min_delay_ms, max_delay_ms)),
	  _current_delay_ms(min_delay_ms)
{
}

void AdaptiveDelayController::RecordSample(uint32_t track_id, int64_t lateness_ms)
{
	auto now = std::chrono::steady_clock::now();

	bool warn_exceed_max	 = false;
	int64_t warn_late_value	 = 0;
	int warn_late_max		 = 0;

	bool warn_max_reached	 = false;
	int warn_max_value		 = 0;

	{
		std::lock_guard<std::mutex> lock(_mu);
		_samples.push_back({now, lateness_ms, track_id});

		while (!_samples.empty() && (now - _samples.front().ts) > kWindow)
		{
			_samples.pop_front();
		}

		// Lateness > Max: even the configured ceiling cannot smooth this frame;
		// receiver almost certainly drops it. Operator should raise Max or
		// investigate source jitter.
		if (lateness_ms > static_cast<int64_t>(_max_delay_ms))
		{
			if ((now - _last_lateness_warn) >= kWarnThrottle)
			{
				_last_lateness_warn = now;
				warn_exceed_max		= true;
				warn_late_value		= lateness_ms;
				warn_late_max		= _max_delay_ms;
			}
		}

		if ((now - _last_recompute) >= kRecomputeInterval)
		{
			Recompute();
			_last_recompute = now;
		}

		if ((now - _last_stats_log) >= kStatsLogInterval &&
			_samples.size() >= kMinSamplesForRecompute)
		{
			_last_stats_log = now;
			LogPerTrackStats();
		}

		// Max-reached transition. Only meaningful when Min < Max (otherwise the
		// delay is intentionally pinned and the warning would be misleading).
		if (_min_delay_ms < _max_delay_ms)
		{
			if (_current_delay_ms >= _max_delay_ms)
			{
				if (!_at_max && (now - _last_max_warn) >= kWarnThrottle)
				{
					_last_max_warn	 = now;
					_at_max			 = true;
					warn_max_reached = true;
					warn_max_value	 = _max_delay_ms;
				}
			}
			else
			{
				_at_max = false;
			}
		}
	}

	if (warn_exceed_max)
	{
		logtw("Lateness %lldms exceeds configured Pacer Max %dms — Pacer cannot smooth this frame; receiver likely drops it. Consider raising Max or investigating source jitter.",
			  static_cast<long long>(warn_late_value),
			  warn_late_max);
	}
	if (warn_max_reached)
	{
		logtw("Adaptive delay reached configured Max %dms — Max may be too low for current conditions",
			  warn_max_value);
	}
}

int AdaptiveDelayController::GetCurrentDelayMs()
{
	std::lock_guard<std::mutex> lock(_mu);
	return _current_delay_ms;
}

void AdaptiveDelayController::Recompute()
{
	if (_samples.size() < kMinSamplesForRecompute)
	{
		return;
	}

	std::vector<int64_t> values;
	values.reserve(_samples.size());
	for (const auto &s : _samples)
	{
		values.push_back(s.lateness_ms);
	}
	std::sort(values.begin(), values.end());

	size_t idx = static_cast<size_t>(kTargetPercentile * (values.size() - 1));
	int64_t p_value = values[idx];

	// Negative percentile means anchor settled at a path slower than typical;
	// no buffer needed beyond Min in that case. Floor at 0 so margin is the
	// effective minimum buffer size when jitter is below the floor.
	int desired_ms = static_cast<int>(std::max<int64_t>(p_value, 0)) + kMarginMs;
	desired_ms	   = std::clamp(desired_ms, _min_delay_ms, _max_delay_ms);

	_current_delay_ms = desired_ms;
}

// Caller holds _mu.
void AdaptiveDelayController::LogPerTrackStats()
{
	// Group samples by track_id.
	std::map<uint32_t, std::vector<int64_t>> per_track;
	for (const auto &s : _samples)
	{
		per_track[s.track_id].push_back(s.lateness_ms);
	}

	auto pct = [](std::vector<int64_t> &sorted, double p) {
		if (sorted.empty()) return int64_t{0};
		size_t idx = static_cast<size_t>(p * (sorted.size() - 1));
		return sorted[idx];
	};

	ov::String msg;
	msg.Format("[PacerStats] current_delay=%dms total_samples=%zu min=%dms max=%dms",
			   _current_delay_ms, _samples.size(), _min_delay_ms, _max_delay_ms);

	for (auto &[tid, vals] : per_track)
	{
		std::sort(vals.begin(), vals.end());
		int64_t p5	= pct(vals, 0.05);
		int64_t p50 = pct(vals, 0.50);
		int64_t p95 = pct(vals, 0.95);
		int64_t mn	= vals.front();
		int64_t mx	= vals.back();

		msg.AppendFormat("\n  track=%u count=%zu p5=%lldms p50=%lldms p95=%lldms min=%lldms max=%lldms",
						 tid, vals.size(),
						 static_cast<long long>(p5),
						 static_cast<long long>(p50),
						 static_cast<long long>(p95),
						 static_cast<long long>(mn),
						 static_cast<long long>(mx));
	}

	logtd("%s", msg.CStr());
}
