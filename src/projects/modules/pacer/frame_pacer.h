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
#include <mutex>

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

	FramePacer(int64_t timebase_num, int64_t timebase_den, uint32_t fallback_delay_ms);

	// Set the scheduler (shared DelayQueue) and the dispatcher callback that
	// will be invoked by the scheduler thread at the target time.
	void Init(std::shared_ptr<ov::DelayQueue> scheduler, DispatchFn dispatcher);

	// Optional: attach a stream-shared adaptive controller. When set, the
	// pacer queries it for the current delay (instead of the fallback fixed
	// value) and reports observed lateness back to it.
	void SetAdaptiveController(std::shared_ptr<AdaptiveDelayController> controller);

	// Schedule the frame for delayed dispatch.
	void Push(const std::shared_ptr<MediaPacket> &packet);

private:
	int64_t _timebase_num;
	int64_t _timebase_den;
	uint32_t _fallback_delay_ms;

	std::shared_ptr<AdaptiveDelayController> _adaptive_controller;

	std::shared_ptr<ov::DelayQueue> _scheduler;
	DispatchFn _dispatcher;

	std::mutex _mu;
	bool _anchor_set = false;
	int64_t _anchor_pts_us = 0;
	std::chrono::steady_clock::time_point _anchor_arrival;
	std::chrono::steady_clock::time_point _last_push;
	std::chrono::steady_clock::time_point _last_drift_warn;
};
