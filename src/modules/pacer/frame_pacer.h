//==============================================================================
//
//  OvenMediaEngine
//
//==============================================================================
#pragma once

#include <base/mediarouter/media_buffer.h>
#include <base/ovlibrary/delay_queue.h>
#include <base/ovlibrary/ovlibrary.h>

#include <chrono>
#include <functional>
#include <memory>
#include <vector>

#include "adaptive_delay_controller.h"

// Per-track PTS-anchored sender-side frame pacer.
//
// Schedules each frame's dispatch on a shared ov::DelayQueue (one worker
// thread shared across all tracks of a stream). Frames may arrive in bursts
// due to encoder delays, network conditions, or sender-side pacing; without
// smoothing, WebRTC players render them at the burst rate, causing uneven
// playback. This pacer dispatches each frame at its PTS-derived expected
// time instead:
//   target = anchor_arrival + (frame.pts - anchor.pts) + delay
// The first frame after a long idle (or the first ever) establishes a fresh
// anchor.
//
// When attached to a shared AdaptiveDelayController, the delay is queried per
// frame and reflects the current adaptive estimate (Min == Max pins it fixed).
class FramePacer
{
public:
	using DispatchFn = std::function<void(const std::shared_ptr<MediaPacket> &)>;

	FramePacer(const ov::String &stream_id, int64_t timebase_num, int64_t timebase_den, uint32_t fallback_delay_ms);

	// Set the scheduler (shared DelayQueue) and the dispatcher callback that
	// will be invoked by the scheduler thread at the target time.
	void Init(std::shared_ptr<ov::DelayQueue> scheduler, DispatchFn dispatcher);

	// Optional: attach a stream-shared adaptive controller. When set, the
	// pacer queries it for the current delay (instead of the fallback fixed
	// value) and reports observed lateness back to it.
	void SetAdaptiveController(std::shared_ptr<AdaptiveDelayController> controller);

	// Schedule the frame for delayed dispatch.
	//
	// arrival_time is the wall-clock moment the frame entered the publisher
	// pipeline (captured at the top of RtcStream::SendVideoFrame / SendAudioFrame).
	// It must be passed in rather than read inside Push() so that any variable
	// processing delay between entry and Push (e.g. the initial buffer flush
	// during the Started→Prepared transition) does not leak into the anchor.
	void Push(const std::shared_ptr<MediaPacket> &packet,
			  std::chrono::steady_clock::time_point arrival_time);

private:
	ov::String _stream_id;
	int64_t _timebase_num;
	int64_t _timebase_den;
	uint32_t _fallback_delay_ms;

	std::shared_ptr<AdaptiveDelayController> _adaptive_controller;

	std::shared_ptr<ov::DelayQueue> _scheduler;
	DispatchFn _dispatcher;

	ov::Mutex _mu;
	bool _anchor_set OV_GUARDED_BY(_mu) = false;
	int64_t _anchor_pts_us OV_GUARDED_BY(_mu) = 0;
	std::chrono::steady_clock::time_point _anchor_arrival OV_GUARDED_BY(_mu);
	std::chrono::steady_clock::time_point _last_push OV_GUARDED_BY(_mu);
	std::chrono::steady_clock::time_point _last_drift_warn OV_GUARDED_BY(_mu);

	// Anchor calibration. The provisional anchor (first frame after reset)
	// may sit at an extreme of the path-delay distribution; calibration
	// collects N frames and shifts the anchor by their median so baseline
	// lateness centers on zero.
	bool _anchor_calibrated OV_GUARDED_BY(_mu) = false;
	std::vector<int64_t> _calibration_samples_ms OV_GUARDED_BY(_mu);
	// Total frames since last (re)anchor including outliers, used as a
	// timeout when valid samples don't accumulate.
	size_t _calibration_attempts OV_GUARDED_BY(_mu) = 0;
};
