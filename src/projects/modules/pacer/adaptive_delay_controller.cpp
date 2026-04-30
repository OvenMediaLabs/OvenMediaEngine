//==============================================================================
//
//  OvenMediaEngine
//
//==============================================================================
#include "adaptive_delay_controller.h"

#include <algorithm>
#include <vector>

#define OV_LOG_TAG "AdaptiveDelay"

// Rolling window of samples used to compute span. Larger window = more stable
// but slower to adapt to genuine condition changes.
static constexpr auto kWindow = std::chrono::seconds(30);

// Recompute is throttled to this interval (avoids re-scanning per-frame).
static constexpr auto kRecomputeInterval = std::chrono::milliseconds(500);

// Per-recompute decrement when measured demand drops below current delay.
// Up-jumps are immediate (favor safety); down-relaxation is gradual.
static constexpr int kDecreasePerStepMs = 1;

// Don't update current_delay unless the desired value differs by at least this
// many ms. Avoids micro-oscillation that translates into perceived playback
// speed jitter at the receiver.
static constexpr int kStabilityBandMs = 20;

// Use percentiles instead of raw max/min, so a single late frame doesn't drive
// the delay up. Symmetric: high percentile minus low percentile = robust span.
static constexpr double kHighPercentile = 0.95;
static constexpr double kLowPercentile	= 0.05;

// Below this many samples, percentile estimate is unreliable; skip recompute.
static constexpr size_t kMinSamplesForRecompute = 30;

// Safety margin added on top of measured span / observed peak lateness when
// computing the target delay. Internal constant — not user-tunable.
static constexpr int kMarginMs = 30;

AdaptiveDelayController::AdaptiveDelayController(int min_delay_ms, int max_delay_ms)
	: _min_delay_ms(min_delay_ms),
	  _max_delay_ms(std::max(min_delay_ms, max_delay_ms)),
	  _current_delay_ms(min_delay_ms)
{
}

void AdaptiveDelayController::RecordSample(int64_t lateness_ms)
{
	auto now = std::chrono::steady_clock::now();

	std::lock_guard<std::mutex> lock(_mu);
	_samples.push_back({now, lateness_ms});

	while (!_samples.empty() && (now - _samples.front().ts) > kWindow)
	{
		_samples.pop_front();
	}

	// Fast-up: a single sample whose lateness exceeds the current delay means
	// smoothing was insufficient for that frame and a drop likely occurred.
	// Jump up immediately, bypassing percentile-based stability logic.
	if (lateness_ms > static_cast<int64_t>(_current_delay_ms))
	{
		int target = static_cast<int>(lateness_ms) + kMarginMs;
		target	   = std::clamp(target, _min_delay_ms, _max_delay_ms);
		if (target > _current_delay_ms)
		{
			_current_delay_ms = target;
		}
	}

	if ((now - _last_recompute) >= kRecomputeInterval)
	{
		Recompute();
		_last_recompute = now;
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

	auto pct = [&](double p) {
		size_t idx = static_cast<size_t>(p * (values.size() - 1));
		return values[idx];
	};

	int64_t high_lateness = pct(kHighPercentile);
	int64_t low_lateness  = pct(kLowPercentile);

	int desired_ms = static_cast<int>(high_lateness - low_lateness) + kMarginMs;
	desired_ms	   = std::clamp(desired_ms, _min_delay_ms, _max_delay_ms);

	int diff = desired_ms - _current_delay_ms;

	if (diff > 0)
	{
		// Up: only jump when desired exceeds current by stability band.
		// Otherwise do nothing — current is already in the safe zone.
		if (diff >= kStabilityBandMs)
		{
			_current_delay_ms = desired_ms;
		}
	}
	else if (-diff >= kStabilityBandMs)
	{
		// Down: gradual, only when desired is meaningfully below current.
		_current_delay_ms = std::max(desired_ms, _current_delay_ms - kDecreasePerStepMs);
	}
}
