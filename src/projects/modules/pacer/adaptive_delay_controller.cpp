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

// Target percentile of recent lateness. The Pacer absorbs up to this; the
// remaining tail passes through to the receiver's jitter buffer.
static constexpr double kTargetPercentile = 0.99;

// Below this many samples, percentile estimate is unreliable; skip recompute.
static constexpr size_t kMinSamplesForRecompute = 30;

// Headroom added on top of the percentile.
static constexpr int kMarginMs = 20;

// EMA coefficient applied when the desired delay drops below the current one.
// Increases are immediate (alpha=1) so a fresh jitter spike isn't lost;
// decreases are eased so a shrinking buffer doesn't show up to the viewer
// as variable playback rate.
static constexpr double kRelaxAlpha = 0.1;

// Warning rate limit (per warning type).
static constexpr auto kWarnThrottle = std::chrono::seconds(1);

// Periodic per-track lateness statistics dump (for measurement / tuning).
static constexpr auto kStatsLogInterval = std::chrono::seconds(5);

AdaptiveDelayController::AdaptiveDelayController(const ov::String &stream_id, int min_delay_ms, int max_delay_ms)
	: _stream_id(stream_id),
	  _min_delay_ms(min_delay_ms),
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
		logtw("[%s] Lateness %lldms exceeds configured Pacer Max %dms — Pacer cannot smooth this frame; receiver likely drops it. Consider raising Max or investigating source jitter.",
			  _stream_id.CStr(),
			  static_cast<long long>(warn_late_value),
			  warn_late_max);
	}
	if (warn_max_reached)
	{
		logtw("[%s] Adaptive delay reached configured Max %dms — Max may be too low for current conditions",
			  _stream_id.CStr(),
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

	// Per-track percentile, take the max. Mixing tracks into one sorted window
	// biases the percentile toward the higher-rate, lower-lateness track
	// (typically audio), under-buffering the worse track. Per-track max keeps
	// A/V sync (one shared delay) while letting any track's tail size it.
	std::map<uint32_t, std::vector<int64_t>> per_track;
	for (const auto &s : _samples)
	{
		per_track[s.track_id].push_back(s.lateness_ms);
	}

	// Per-track minimum sample count to trust that track's percentile.
	static constexpr size_t kMinPerTrack = 10;

	int64_t worst_pvalue = 0;
	bool have_value		 = false;
	for (auto &[tid, vals] : per_track)
	{
		if (vals.size() < kMinPerTrack)
		{
			continue;
		}
		std::sort(vals.begin(), vals.end());
		size_t idx		= static_cast<size_t>(kTargetPercentile * (vals.size() - 1));
		int64_t p_value = vals[idx];
		if (!have_value || p_value > worst_pvalue)
		{
			worst_pvalue = p_value;
			have_value	 = true;
		}
	}
	if (!have_value)
	{
		return;
	}

	// Floor at 0: negative percentile means jitter is below the floor, in
	// which case the margin defines the minimum buffer.
	int desired_ms = static_cast<int>(std::max<int64_t>(worst_pvalue, 0)) + kMarginMs;
	desired_ms	   = std::clamp(desired_ms, _min_delay_ms, _max_delay_ms);

	if (desired_ms >= _current_delay_ms)
	{
		_current_delay_ms = desired_ms;
	}
	else
	{
		double smoothed	  = kRelaxAlpha * static_cast<double>(desired_ms) +
							(1.0 - kRelaxAlpha) * static_cast<double>(_current_delay_ms);
		_current_delay_ms = std::clamp(static_cast<int>(smoothed), _min_delay_ms, _max_delay_ms);
	}
}

// Caller holds _mu.
void AdaptiveDelayController::LogPerTrackStats()
{
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
	msg.Format("[%s] [PacerStats] current_delay=%dms total_samples=%zu min=%dms max=%dms",
			   _stream_id.CStr(), _current_delay_ms, _samples.size(), _min_delay_ms, _max_delay_ms);

	for (auto &[tid, vals] : per_track)
	{
		std::sort(vals.begin(), vals.end());
		msg.AppendFormat("\n  track=%u count=%zu p5=%lldms p50=%lldms p95=%lldms p99=%lldms min=%lldms max=%lldms",
						 tid, vals.size(),
						 static_cast<long long>(pct(vals, 0.05)),
						 static_cast<long long>(pct(vals, 0.50)),
						 static_cast<long long>(pct(vals, 0.95)),
						 static_cast<long long>(pct(vals, 0.99)),
						 static_cast<long long>(vals.front()),
						 static_cast<long long>(vals.back()));
	}

	logtd("%s", msg.CStr());
}
