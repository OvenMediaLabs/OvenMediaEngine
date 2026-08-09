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

void SkipFramesController::Configure(int32_t skip_frames_conf)
{
	_skip_frames_conf = skip_frames_conf;
	_skip_frames	  = skip_frames_conf;
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

	_weighted_avg_frame_processing_time_us	= (_weighted_avg_frame_processing_time_us * (1.0 - kFrameTimeEmaAlpha)) + (processing_time_us * kFrameTimeEmaAlpha);
	_weighted_avg_frame_handoff_time_us		= (_weighted_avg_frame_handoff_time_us * (1.0 - kFrameTimeEmaAlpha)) + (handoff_time_us * kFrameTimeEmaAlpha);
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

	if (elapsed_since_check_ms <= kEvaluationIntervalMs)
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
	double frame_budget_us = _weighted_avg_frame_processing_time_us + _weighted_avg_frame_handoff_time_us;

	if (frame_budget_us <= 0.0 || observation.cadence_fps <= 0.0)
	{
		return std::nullopt;
	}

	// The most frames per second the budget allows, less a safety margin.
	double peak_fps		   = (1000000.0 / frame_budget_us);
	double sustainable_fps = peak_fps * kSafetyMarginRatio;

	// How far the cadence has to be divided down to land on the sustainable rate.
	auto next_skip_frames = static_cast<int32_t>(std::ceil(observation.cadence_fps / sustainable_fps - 1.0));
	if (next_skip_frames > observation.cadence_fps - 1)
	{
		next_skip_frames = static_cast<int32_t>(std::floor(observation.cadence_fps - 1));
	}
	else if (next_skip_frames < FilterFps::SkipFramesMin)
	{
		next_skip_frames = FilterFps::SkipFramesMin;
	}

	// Busy is not the same as overloaded. The encoder queue is only two frames deep, so the
	// handoff blocks even on a chain that keeps up. Idle time is what tells the two apart.
	bool is_saturated = (utilization >= kSaturationRatio);

	// A saturated thread also has to be losing ground. Zero means the FPS filter has not
	// measured a second yet, not that nothing was consumed.
	bool is_falling_behind = (observation.expected_input_fps > 0.0) &&
							 (observation.actual_input_fps > 0.0) &&
							 (observation.actual_input_fps < observation.expected_input_fps * kInputKeepUpRatio);

	if (is_saturated == true && is_falling_behind == true)
	{
		_bottleneck_count++;

		// The budget decays between stalls, so without this the recovery branch below could
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
		bool has_headroom = (utilization < kRecoveryMaxBusyRatio) || (is_falling_behind == false);

		next_skip_frames = has_headroom ? FilterFps::SkipFramesMin : _skip_frames;
	}

	if (next_skip_frames > _skip_frames)
	{
		// One bad window is a hiccup; a bottleneck is still there on the next one.
		if (_bottleneck_count < kBottleneckConfirmCount)
		{
			next_skip_frames = _skip_frames;
		}
		// Rise in single steps, so no single window can collapse the output rate.
		else if (next_skip_frames > _skip_frames + kMaxIncreasePerWindow)
		{
			next_skip_frames = _skip_frames + kMaxIncreasePerWindow;
		}
	}

	// Every value labelled, every pair actual/target, times in ms - readable on its own.
	Result result;
	result.skip_frames = _skip_frames;
	result.metrics	   = ov::String::FormatString("Input: %.1f/%.1ffps, Busy: %.1f%%, Budget: %.2fms (proc %.2f + handoff %.2f), Capacity: %.1ffps, Output: %.1f/%.1ffps (cadence %.1f)",
												  observation.actual_input_fps, observation.expected_input_fps,
												  utilization * 100.0,
												  frame_budget_us / 1000.0, _weighted_avg_frame_processing_time_us / 1000.0, _weighted_avg_frame_handoff_time_us / 1000.0,
												  sustainable_fps,
												  observation.actual_output_fps, observation.expected_output_fps, observation.cadence_fps);

	// Increase skip frames immediately when bottleneck occurs.
	if (_skip_frames < next_skip_frames)
	{
		result.decision	   = Decision::Bottleneck;
		result.skip_frames = next_skip_frames;

		_skip_frames		  = next_skip_frames;
		_last_changed_time_ms = curr_time_ms;
	}
	// Decrease skip frames slowly when the system is recovering.
	else if (_skip_frames > next_skip_frames)
	{
		if (elapsed_since_change_ms > kRecoveryHoldIntervalMs)
		{
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
	// Keep skip frames unchanged when the system is stable.
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

	// The level counts frames within a cadence, so it only transfers between two automatic
	// controllers. A configured level was already applied by Configure().
	if (IsAutomatic() == true && previous.IsAutomatic() == true)
	{
		_skip_frames	  = previous._skip_frames;
		_bottleneck_count = previous._bottleneck_count;

		// Both carry, or neither does: Evaluate() re-seeds the pair whenever either is zero,
		// which would restart the recovery hold at every replacement.
		_last_check_time_ms	  = previous._last_check_time_ms;
		_last_changed_time_ms = previous._last_changed_time_ms;
	}
}
