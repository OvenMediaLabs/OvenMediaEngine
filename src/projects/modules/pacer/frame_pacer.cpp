//==============================================================================
//
//  OvenMediaEngine
//
//==============================================================================
#include "frame_pacer.h"

#define OV_LOG_TAG "FramePacer"

// Reset the anchor if no packet has been pushed for this duration. Avoids
// scheduling far in the future when the source pauses then resumes.
static constexpr auto kAnchorIdleResetThreshold = std::chrono::seconds(2);

// If a frame's computed dispatch time would be more than this many delays in
// the future, the anchor is considered drifted (e.g., burst push of catch-up
// packets, or PTS clock running faster than wall clock). Reset.
static constexpr int kAnchorDriftMultiplier = 3;

// Per-track warning rate limit.
static constexpr auto kWarnThrottle = std::chrono::seconds(1);

FramePacer::FramePacer(int64_t timebase_num, int64_t timebase_den, uint32_t fallback_delay_ms)
	: _timebase_num(timebase_num),
	  _timebase_den(timebase_den),
	  _fallback_delay_ms(fallback_delay_ms)
{
}

void FramePacer::Init(std::shared_ptr<ov::DelayQueue> scheduler, DispatchFn dispatcher)
{
	_scheduler	= std::move(scheduler);
	_dispatcher = std::move(dispatcher);
}

void FramePacer::SetAdaptiveController(std::shared_ptr<AdaptiveDelayController> controller)
{
	_adaptive_controller = std::move(controller);
}

void FramePacer::Push(const std::shared_ptr<MediaPacket> &packet)
{
	if (_scheduler == nullptr || _dispatcher == nullptr || packet == nullptr)
	{
		return;
	}

	int after_ms = 0;

	bool warn_drift				= false;
	int64_t warn_drift_lateness = 0;
	int64_t warn_drift_delta	= 0;
	uint32_t warn_drift_delay	= 0;
	uint32_t warn_drift_track	= 0;

	{
		std::lock_guard<std::mutex> lock(_mu);

		auto now = std::chrono::steady_clock::now();

		// Anchor reset on first push or after long idle
		if (!_anchor_set || (now - _last_push) > kAnchorIdleResetThreshold)
		{
			int64_t pts_us = (_timebase_den == 0)
								 ? 0
								 : static_cast<int64_t>(static_cast<double>(packet->GetPts()) *
														static_cast<double>(_timebase_num) * 1000000.0 /
														static_cast<double>(_timebase_den));
			_anchor_pts_us	= pts_us;
			_anchor_arrival = now;
			_anchor_set		= true;
		}
		_last_push = now;

		int64_t pts_us = (_timebase_den == 0)
							 ? 0
							 : static_cast<int64_t>(static_cast<double>(packet->GetPts()) *
													static_cast<double>(_timebase_num) * 1000000.0 /
													static_cast<double>(_timebase_den));
		int64_t pts_diff_us = pts_us - _anchor_pts_us;

		// Lateness vs anchor-based expected arrival (in ms)
		auto expected_arrival = _anchor_arrival + std::chrono::microseconds(pts_diff_us);
		int64_t lateness_ms	  = std::chrono::duration_cast<std::chrono::milliseconds>(now - expected_arrival).count();

		uint32_t effective_delay_ms = _adaptive_controller
										  ? static_cast<uint32_t>(_adaptive_controller->GetCurrentDelayMs())
										  : _fallback_delay_ms;

		auto target = expected_arrival + std::chrono::milliseconds(effective_delay_ms);

		auto delta_ms = std::chrono::duration_cast<std::chrono::milliseconds>(target - now).count();

		// Anchor drift safety: if target is too far in the future compared to the
		// effective delay, the anchor has drifted (e.g., a burst of catch-up
		// packets or source PTS running faster than wall clock). Reset.
		if (effective_delay_ms > 0 &&
			delta_ms > static_cast<int64_t>(effective_delay_ms) * kAnchorDriftMultiplier)
		{
			if ((now - _last_drift_warn) >= kWarnThrottle)
			{
				_last_drift_warn	 = now;
				warn_drift			 = true;
				warn_drift_lateness	 = lateness_ms;
				warn_drift_delta	 = delta_ms;
				warn_drift_delay	 = effective_delay_ms;
				warn_drift_track	 = packet->GetTrackId();
			}

			_anchor_pts_us	= pts_us;
			_anchor_arrival = now;
			delta_ms		= effective_delay_ms;
			lateness_ms		= 0;
		}

		// Report lateness for adaptive feedback
		if (_adaptive_controller)
		{
			_adaptive_controller->RecordSample(lateness_ms);
		}

		after_ms = (delta_ms < 0) ? 0 : static_cast<int>(delta_ms);
	}

	if (warn_drift)
	{
		logtw("Anchor drift reset on track %u (lateness=%lldms, computed delta=%lldms, current delay=%ums) — PTS clock may be running faster than wall clock or catch-up burst pushed",
			  warn_drift_track,
			  static_cast<long long>(warn_drift_lateness),
			  static_cast<long long>(warn_drift_delta),
			  warn_drift_delay);
	}

	// Capture by value so the lambda is independent of FramePacer lifetime.
	auto dispatcher_copy = _dispatcher;
	auto packet_copy	 = packet;
	_scheduler->Push(
		[dispatcher_copy, packet_copy](void *) -> ov::DelayQueueAction {
			if (dispatcher_copy)
			{
				dispatcher_copy(packet_copy);
			}
			return ov::DelayQueueAction::Stop;
		},
		after_ms);
}
