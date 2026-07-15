//==============================================================================
//
//  OvenMediaEngine
//
//==============================================================================
#include "frame_pacer.h"

#include <algorithm>
#include <vector>

#define OV_LOG_TAG "FramePacer"

// Idle longer than this resets the anchor. Catches the WebRTC publisher
// startup pattern where the first keyframe is followed by a brief idle
// (Chrome BWE/pacer throttle) that bakes a baseline lateness offset.
// Loose enough not to trigger on normal frame jitter at 30/60fps.
static constexpr auto kAnchorIdleResetThreshold = std::chrono::milliseconds(500);

// Treat anchor as drifted when target dispatch is this many delays in the
// future (catch-up burst or PTS running faster than wall clock).
static constexpr int kAnchorDriftMultiplier = 3;

// Number of valid frames after each (re)anchor used to calibrate the
// anchor onto the path-delay median.
static constexpr size_t kCalibrationFrames = 30;

// Force calibration after this many total frames even without enough
// valid samples (bounds worst-case startup latency).
static constexpr size_t kCalibrationMaxAttempts = 90;

// Per-track warning rate limit.
static constexpr auto kWarnThrottle = std::chrono::seconds(1);

FramePacer::FramePacer(const ov::String &stream_id, int64_t timebase_num, int64_t timebase_den, uint32_t fallback_delay_ms)
	: _stream_id(stream_id),
	  _timebase_num(timebase_num),
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

void FramePacer::Push(const std::shared_ptr<MediaPacket> &packet,
					  std::chrono::steady_clock::time_point arrival_time)
{
	if (_scheduler == nullptr || _dispatcher == nullptr || packet == nullptr)
	{
		return;
	}

	int after_ms = 0;

	bool warn_drift					= false;
	bool warn_drift_was_calibrated	= false;
	int64_t warn_drift_lateness		= 0;
	int64_t warn_drift_delta		= 0;
	uint32_t warn_drift_delay		= 0;
	uint32_t warn_drift_track		= 0;

	bool calibrated			   = false;
	int64_t calib_shift_ms	   = 0;
	size_t calib_sample_count  = 0;
	uint32_t calib_track_id	   = 0;

	{
		ov::LockGuard<ov::Mutex> lock(_mu);

		auto now = arrival_time;

		// Anchor reset on first push or after long idle. Uses current frame
		// as a provisional anchor; calibration shifts it to the median.
		if (!_anchor_set || (now - _last_push) > kAnchorIdleResetThreshold)
		{
			int64_t pts_us = (_timebase_den == 0)
								 ? 0
								 : static_cast<int64_t>(static_cast<double>(packet->GetPts()) *
														static_cast<double>(_timebase_num) * 1000000.0 /
														static_cast<double>(_timebase_den));
			_anchor_pts_us			= pts_us;
			_anchor_arrival			= now;
			_anchor_set				= true;
			_anchor_calibrated		= false;
			_calibration_samples_ms.clear();
			_calibration_attempts	= 0;
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

		// Anchor drift safety. Reset if target sits too far in the future.
		if (effective_delay_ms > 0 &&
			delta_ms > static_cast<int64_t>(effective_delay_ms) * kAnchorDriftMultiplier)
		{
			if ((now - _last_drift_warn) >= kWarnThrottle)
			{
				_last_drift_warn			= now;
				warn_drift					= true;
				warn_drift_was_calibrated	= _anchor_calibrated;
				warn_drift_lateness			= lateness_ms;
				warn_drift_delta			= delta_ms;
				warn_drift_delay			= effective_delay_ms;
				warn_drift_track			= packet->GetTrackId();
			}

			_anchor_pts_us			= pts_us;
			_anchor_arrival			= now;
			delta_ms				= effective_delay_ms;
			lateness_ms				= 0;
			_anchor_calibrated		= false;
			_calibration_samples_ms.clear();
			_calibration_attempts	= 0;
		}
		else if (!_anchor_calibrated)
		{
			_calibration_attempts++;

			// Outlier guard: drop positive spikes above the smoothing ceiling
			// so they don't pull the median. Negatives stay (they're part of
			// the burst pattern; runaways trigger drift reset above).
			int64_t calib_outlier_max = _adaptive_controller
											? static_cast<int64_t>(_adaptive_controller->GetMaxDelayMs())
											: static_cast<int64_t>(_fallback_delay_ms);
			if (lateness_ms <= calib_outlier_max)
			{
				_calibration_samples_ms.push_back(lateness_ms);
			}

			// Calibrate when enough valid samples, or when timeout fires
			// (force-completes calibration so we don't spin forever on
			// pervasively over-ceiling jitter).
			bool enough  = _calibration_samples_ms.size() >= kCalibrationFrames;
			bool timeout = _calibration_attempts >= kCalibrationMaxAttempts;
			if (enough || timeout)
			{
				int64_t median_ms = 0;
				if (!_calibration_samples_ms.empty())
				{
					auto mid = _calibration_samples_ms.begin() + _calibration_samples_ms.size() / 2;
					std::nth_element(_calibration_samples_ms.begin(), mid, _calibration_samples_ms.end());
					median_ms = *mid;

					_anchor_arrival += std::chrono::milliseconds(median_ms);

					// Re-evaluate the current frame against the calibrated anchor.
					expected_arrival = _anchor_arrival + std::chrono::microseconds(pts_diff_us);
					lateness_ms		 = std::chrono::duration_cast<std::chrono::milliseconds>(now - expected_arrival).count();
					target			 = expected_arrival + std::chrono::milliseconds(effective_delay_ms);
					delta_ms		 = std::chrono::duration_cast<std::chrono::milliseconds>(target - now).count();
				}

				_anchor_calibrated		   = true;
				calibrated				   = true;
				calib_shift_ms			   = median_ms;
				calib_sample_count		   = _calibration_samples_ms.size();
				calib_track_id			   = packet->GetTrackId();
				_calibration_samples_ms.clear();
			}
		}

		if (_adaptive_controller)
		{
			_adaptive_controller->RecordSample(packet->GetTrackId(), lateness_ms);
		}

		after_ms = (delta_ms < 0) ? 0 : static_cast<int>(delta_ms);
	}

	if (warn_drift)
	{
		// Startup-phase drift is the expected publisher backlog flush (info).
		// Post-calibration drift is a genuine anomaly (warning).
		if (warn_drift_was_calibrated)
		{
			logtw("[%s] Anchor drift reset on track %u (lateness=%lldms, computed delta=%lldms, current delay=%ums) — PTS clock may be running faster than wall clock or catch-up burst pushed",
				  _stream_id.CStr(),
				  warn_drift_track,
				  static_cast<long long>(warn_drift_lateness),
				  static_cast<long long>(warn_drift_delta),
				  warn_drift_delay);
		}
		else
		{
			logti("[%s] Startup anchor drift reset on track %u (lateness=%lldms) — initial publisher backlog flush, will recalibrate",
				  _stream_id.CStr(),
				  warn_drift_track,
				  static_cast<long long>(warn_drift_lateness));
		}
	}

	if (calibrated)
	{
		if (calib_sample_count == 0)
		{
			logtw("[%s] Anchor calibration on track %u completed with no usable samples "
				  "— all %zu initial frames exceeded Pacer Max. Source jitter "
				  "exceeds Pacer's smoothing ceiling; provisional anchor kept.",
				  _stream_id.CStr(),
				  calib_track_id, kCalibrationMaxAttempts);
		}
		else
		{
			logti("[%s] Anchor calibrated on track %u by %lldms (%zu/%zu valid samples)",
				  _stream_id.CStr(),
				  calib_track_id,
				  static_cast<long long>(calib_shift_ms),
				  calib_sample_count,
				  kCalibrationFrames);
		}
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
