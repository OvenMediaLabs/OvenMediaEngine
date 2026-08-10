//==============================================================================
//
//  Transcode
//
//  Created by Kwon Keuk Han
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================

#include "filter_skipframes_controller.h"

#include <algorithm>
#include <cmath>

#include "filter_fps.h"

namespace
{
	// how often the level is reconsidered
	constexpr int64_t EVALUATION_INTERVAL_MS	= 1000;
	// how long a level must stand before it may drop
	constexpr int64_t RECOVERY_HOLD_INTERVAL_MS	= 5000;
	// ceiling the hold backs off to while probes keep failing
	constexpr int64_t RECOVERY_HOLD_MAX_MS		= 60000;
	// aim at 90% of what the frame cost allows
	constexpr double SAFETY_MARGIN_RATIO		= 0.9;
	// busy this much of a window counts as saturated
	constexpr double SATURATION_RATIO			= 0.95;
	// below this the thread has room to try one level down
	constexpr double RECOVERY_MAX_BUSY_RATIO	= 0.80;
	// share of the input rate the thread must consume
	constexpr double INPUT_KEEP_UP_RATIO		= 0.95;
	// windows that must agree before the level rises
	constexpr int32_t BOTTLENECK_CONFIRM_COUNT	= 2;
	// steps the level may rise in one window
	constexpr int32_t MAX_INCREASE_PER_WINDOW	= 1;

	// how long a step down must stand before it counts
	constexpr int64_t PROBE_SETTLE_INTERVAL_MS = EVALUATION_INTERVAL_MS * (BOTTLENECK_CONFIRM_COUNT + 1);

	// Weight given to the newest sample in the per-frame time averages.
	constexpr double FRAME_TIME_EMA_ALPHA = 0.1;
}  // namespace

void SkipFramesController::Configure(int32_t skip_frames_conf)
{
	_skip_frames_conf = skip_frames_conf;
	_skip_frames	  = skip_frames_conf;
	_recovery_hold_ms = RECOVERY_HOLD_INTERVAL_MS;
}

int32_t SkipFramesController::GetConfiguredSkipFrames() const
{
	return _skip_frames_conf;
}

bool SkipFramesController::IsEnabled() const
{
	return (_skip_frames_conf >= FilterFps::SkipFramesMin);
}

bool SkipFramesController::IsAutomatic() const
{
	return (_skip_frames_conf == FilterFps::SkipFramesMin);
}

int32_t SkipFramesController::GetSkipFrames() const
{
	return _skip_frames;
}

void SkipFramesController::AddProcessingTime(int64_t elapsed_us)
{
	// Only the automatic level reads any of this; otherwise the window never closes.
	if (IsAutomatic() == false)
	{
		return;
	}

	_pending_processing_time_us += elapsed_us;
	_window_busy_time_us += elapsed_us;
}

void SkipFramesController::AddBusyTime(int64_t elapsed_us)
{
	if (IsAutomatic() == false)
	{
		return;
	}

	_window_busy_time_us += elapsed_us;
}

void SkipFramesController::AddHandoffTime(int64_t elapsed_us)
{
	if (IsAutomatic() == false)
	{
		return;
	}

	// Charged whole: capping a sample would also cap the level the controller can reach.
	// One-off stalls are filtered later, when the decision is made.
	_window_busy_time_us += elapsed_us;
	_pending_handoff_time_us += elapsed_us;
}

void SkipFramesController::CommitFrame(int64_t elapsed_us)
{
	if (IsAutomatic() == false)
	{
		return;
	}

	auto processing_time_us = _pending_processing_time_us + elapsed_us;
	auto handoff_time_us	= _pending_handoff_time_us;

	// Only this round: the earlier ones went into the window when they were measured.
	_window_busy_time_us += elapsed_us;

	DiscardPending();

	_weighted_avg_frame_processing_time_us	= (_weighted_avg_frame_processing_time_us * (1.0 - FRAME_TIME_EMA_ALPHA)) + (processing_time_us * FRAME_TIME_EMA_ALPHA);
	_weighted_avg_frame_handoff_time_us		= (_weighted_avg_frame_handoff_time_us * (1.0 - FRAME_TIME_EMA_ALPHA)) + (handoff_time_us * FRAME_TIME_EMA_ALPHA);
}

void SkipFramesController::DiscardPending()
{
	_pending_processing_time_us = 0;
	_pending_handoff_time_us	= 0;
}

std::optional<SkipFramesController::Result> SkipFramesController::Evaluate(int64_t curr_time_ms, const Observation &observation)
{
	if (IsAutomatic() == false)
	{
		return std::nullopt;
	}

	if (_last_check_time_ms == 0 || _last_changed_time_ms == 0)
	{
		_last_check_time_ms	  = curr_time_ms;
		_last_changed_time_ms = curr_time_ms;

		// Session startup was measured before any window opened; it belongs to none.
		_window_busy_time_us = 0;
	}

	auto elapsed_since_check_ms	 = curr_time_ms - _last_check_time_ms;
	auto elapsed_since_change_ms = curr_time_ms - _last_changed_time_ms;

	if (elapsed_since_check_ms <= EVALUATION_INTERVAL_MS)
	{
		return std::nullopt;
	}
	_last_check_time_ms = curr_time_ms;

	// Closed here, before the early returns below can skip the reset.
	double window_us   = static_cast<double>(elapsed_since_check_ms) * 1000.0;
	double utilization = static_cast<double>(_window_busy_time_us) / window_us;

	_window_busy_time_us = 0;

	// A slow rescaler and a backed-up encoder both throttle the thread, and it cannot outrun
	// their sum, so the decision uses the total.
	double frame_cost_us = _weighted_avg_frame_processing_time_us + _weighted_avg_frame_handoff_time_us;

	if (frame_cost_us <= 0.0 || observation.max_output_fps <= 0.0)
	{
		return std::nullopt;
	}

	// The most frames per second the cost allows, less a safety margin.
	double peak_fps		   = (1000000.0 / frame_cost_us);
	double sustainable_fps = peak_fps * SAFETY_MARGIN_RATIO;

	// How far the output ceiling has to be divided down to land on the sustainable rate.
	auto next_skip_frames = static_cast<int32_t>(std::ceil(observation.max_output_fps / sustainable_fps - 1.0));
	if (next_skip_frames > observation.max_output_fps - 1)
	{
		next_skip_frames = static_cast<int32_t>(std::floor(observation.max_output_fps - 1));
	}

	// Prevent the level from going below the minimum, which is also the disabled value.
	if (next_skip_frames < FilterFps::SkipFramesMin)
	{
		next_skip_frames = FilterFps::SkipFramesMin;
	}

	if (observation.expected_input_fps <= 0.0 || observation.actual_input_fps <= 0.0)
	{
		return std::nullopt;
	}

	// Busy is not the same as overloaded. The encoder queue is only two frames deep, so the
	// handoff blocks even on a chain that keeps up. Idle time is what tells the two apart.
	bool is_fully_busy = (utilization >= SATURATION_RATIO);

	bool is_losing_input = (observation.actual_input_fps < observation.expected_input_fps * INPUT_KEEP_UP_RATIO);

	// A fully busy thread also has to be losing input.
	if (is_fully_busy == true && is_losing_input == true)
	{
		_bottleneck_count++;

		// The cost decays between stalls, so without this the recovery branch below could
		// walk the level down while the bottleneck is still there.
		next_skip_frames = std::max(next_skip_frames, _skip_frames);
	}
	else
	{
		_bottleneck_count = 0;

		// A thread keeping up with its input has earned a step down even while it looks
		// busy; gating on utilization alone would pin a level reached during a spike
		// forever. The hold interval makes this a probe - if the level below cannot hold,
		// the next window says so and puts it back.
		bool is_recoverable_busy = (utilization < RECOVERY_MAX_BUSY_RATIO);

		bool may_step_down = (is_recoverable_busy == true) || (is_losing_input == false);

		// The step down lands where the cost allows, not at nothing.
		if (may_step_down == false)
		{
			next_skip_frames = _skip_frames;
		}
	}

	if (next_skip_frames > _skip_frames)
	{
		// Check for a second window of agreement before rising
		if (_bottleneck_count < BOTTLENECK_CONFIRM_COUNT)
		{
			next_skip_frames = _skip_frames;
		}
		
		// The level may only rise one step per window, to avoid overshooting the target.
		else if (next_skip_frames > _skip_frames + MAX_INCREASE_PER_WINDOW)
		{
			next_skip_frames = _skip_frames + MAX_INCREASE_PER_WINDOW;
		}
	}

	// Every value labelled, every pair actual/target, times in ms - readable on its own.
	Result result;
	result.skip_frames = _skip_frames;
	result.metrics	   = ov::String::FormatString("Input: %.1f/%.1ffps, Busy: %.1f%%, Cost: %.2fms (proc %.2f + handoff %.2f), Capacity: %.1ffps, Output: %.1f/%.1ffps (max %.1f)",
												  observation.actual_input_fps, observation.expected_input_fps,
												  utilization * 100.0,
												  frame_cost_us / 1000.0, _weighted_avg_frame_processing_time_us / 1000.0, _weighted_avg_frame_handoff_time_us / 1000.0,
												  sustainable_fps,
												  observation.actual_output_fps, observation.expected_output_fps, observation.max_output_fps);

	// The step down stood. Judged here, not at the next one - that never comes at the minimum.
	if (_probe_deadline_ms > 0 && curr_time_ms > _probe_deadline_ms)
	{
		// Only a step at or below the one that failed proves the pipeline gained room.
		if (_skip_frames <= _known_bad_skip_frames)
		{
			_known_bad_skip_frames = -1;
			_recovery_hold_ms	   = RECOVERY_HOLD_INTERVAL_MS;
		}

		_probe_deadline_ms = 0;
	}

	// [Bottleneck] Increase skip frames immediately when bottleneck occurs.
	if (_skip_frames < next_skip_frames)
	{
		// The step down could not hold. Remember where, and wait longer next time.
		if (_probe_deadline_ms > 0)
		{
			_known_bad_skip_frames = std::max(_known_bad_skip_frames, _skip_frames);
			_recovery_hold_ms	   = std::min(_recovery_hold_ms * 2, RECOVERY_HOLD_MAX_MS);
			_probe_deadline_ms	   = 0;
		}

		result.decision	   = Decision::Bottleneck;
		result.skip_frames = next_skip_frames;

		_skip_frames		  = next_skip_frames;
		_last_changed_time_ms = curr_time_ms;
	}
	// [Recovery & HoldRecovery] Decrease skip frames slowly when the system is recovering.
	else if (_skip_frames > next_skip_frames)
	{
		if (elapsed_since_change_ms > _recovery_hold_ms)
		{
			_probe_deadline_ms = curr_time_ms + PROBE_SETTLE_INTERVAL_MS;

			// Come down 20% at a time, never in one jump.
			int32_t rate_limited_next = _skip_frames - std::max(1, _skip_frames / 5);
			next_skip_frames		  = std::max(rate_limited_next, next_skip_frames);

			result.decision	   = Decision::Recovery;
			result.skip_frames = next_skip_frames;

			_skip_frames		  = next_skip_frames;
			_last_changed_time_ms = curr_time_ms;
		}
		else
		{
			result.decision = Decision::HoldRecovery;
		}
	}
	// [Unchanged] Keep skip frames unchanged when the system is stable.
	else
	{
		result.decision = Decision::Unchanged;
	}

	return result;
}

void SkipFramesController::InheritFrom(const SkipFramesController &previous)
{
	// The load the outgoing controller measured still describes the pipeline.
	_weighted_avg_frame_processing_time_us = previous._weighted_avg_frame_processing_time_us;
	_weighted_avg_frame_handoff_time_us	   = previous._weighted_avg_frame_handoff_time_us;

	// The level is a divisor of the output rate, so it only transfers between two automatic
	// controllers. A configured level was already applied by Configure().
	if (IsAutomatic() == true && previous.IsAutomatic() == true)
	{
		_skip_frames	  = previous._skip_frames;
		_bottleneck_count = previous._bottleneck_count;

		// What the probes learned describes the pipeline, not the filter measuring it.
		_recovery_hold_ms	   = previous._recovery_hold_ms;
		_probe_deadline_ms	   = previous._probe_deadline_ms;
		_known_bad_skip_frames = previous._known_bad_skip_frames;

		// Both carry, or neither does: Evaluate() re-seeds the pair whenever either is zero,
		// which would restart the recovery hold at every replacement.
		_last_check_time_ms	  = previous._last_check_time_ms;
		_last_changed_time_ms = previous._last_changed_time_ms;

		_window_busy_time_us = previous._window_busy_time_us;
	}
}
