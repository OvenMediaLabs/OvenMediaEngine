//==============================================================================
//
//  Transcode
//
//  Created by Kwon Keuk Han
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================

#pragma once

#include <optional>

#include <base/ovlibrary/ovlibrary.h>

// Decides how many frames the FPS filter should skip so a video filter stays within the
// capacity of the pipeline behind it.
//
// Measured over one evaluation window (1s):
//
//   frame cost    what one frame takes: the time spent processing plus the time blocked
//                 handing it off
//   utilization   the share of the window the thread was busy rather than waiting for input
//
// Decided from the two together:
//
//   raise    fully busy AND losing input. The frame cost sets the target
//            level; the rise is one step per window, and only after a second window agrees
//   lower    anything else, once the recovery hold passes - 20% at a time, down to what
//            the frame cost allows rather than straight off
//
// Three traps this is shaped around:
//
//   * Judge against the INPUT rate, never the output rate the level aims at. Skipping
//     lowers that output target, so the filter would keep meeting a bar it just lowered
//     while the upstream queue grew.
//   * Busy is not overloaded. The encoder queue is two frames deep, so the handoff blocks
//     even on a chain that keeps up. Idle time in the window is what tells them apart.
//   * A level that works erases its own evidence. Handoff time falls to zero once the
//     level is right, so a step down is measured where the filter is the bottleneck and
//     probed where the encoder is, with the hold backing off while probes keep failing.
//
// Computation only: no clock, no logging, no FPS filter. The caller supplies the time and
// applies the result, which is what makes the tuning ratios testable.
class SkipFramesController
{
public:
	enum class Decision
	{
		Unchanged,
		Bottleneck,
		Recovery,
		// Wants to come down, but the hold interval has not elapsed.
		HoldRecovery,
	};

	struct Observation
	{
		// The gap between these two is the pipeline backing up.
		double expected_input_fps = 0.0;
		double actual_input_fps	  = 0.0;

		double max_output_fps = 0.0;

		// Log-only - see the note above on why the output rate is not judged against.
		double expected_output_fps = 0.0;
		double actual_output_fps   = 0.0;
	};

	struct Result
	{
		Decision decision = Decision::Unchanged;
		// Equal to the current level except on Bottleneck and Recovery.
		int32_t skip_frames = 0;
		// Everything measured this window, for the caller's log line.
		ov::String metrics;
	};

	// From the output profile: < 0 disabled, 0 automatic, > 0 fixed level.
	void Configure(int32_t skip_frames_conf);
	int32_t GetConfiguredSkipFrames() const;

	bool IsEnabled() const;
	bool IsAutomatic() const;

	// The level currently being asked for. Meaningful once Configure() has run.
	int32_t GetSkipFrames() const;

	// Measurement. All of these run on the filter thread, so none of them synchronize.

	// A round that produced no frame; the time waits for one that does.
	void AddProcessingTime(int64_t elapsed_us);
	// Busy time belonging to no particular frame. Counts toward the window, not the frame cost.
	void AddBusyTime(int64_t elapsed_us);
	// Measurable only after the frame leaves, so it lands one frame late in the average.
	void AddHandoffTime(int64_t elapsed_us);
	// A frame completed: fold everything pending into the averages.
	void CommitFrame(int64_t elapsed_us);
	// Work that will never complete. The busy window is kept - the thread was busy anyway.
	void DiscardPending();

	// Nothing until the interval elapses and there is enough measurement to decide on.
	std::optional<Result> Evaluate(int64_t curr_time_ms, const Observation &observation);

	// Carries the measured load across a filter replacement - the pipeline behind it does
	// not empty out because the filter was replaced.
	void InheritFrom(const SkipFramesController &previous);

private:
	int32_t _skip_frames_conf = -1;
	int32_t _skip_frames	  = -1;

	int64_t _last_check_time_ms	  = 0;
	int64_t _last_changed_time_ms = 0;

	// One bad window is a hiccup, so the level only rises once the diagnosis repeats.
	int32_t _bottleneck_count = 0;

	// Grows while probes keep failing, back to the base interval as soon as one stands.
	int64_t _recovery_hold_ms = 0;

	// The level the outstanding step down was taken from, or 0 when there is none.
	int32_t _probe_origin_level = 0;

	// The per-frame cost, split so the log can name the bottleneck.
	double _weighted_avg_frame_processing_time_us = 0.0;
	double _weighted_avg_frame_handoff_time_us	  = 0.0;

	// Time that has not been charged to a completed frame yet.
	int64_t _pending_processing_time_us = 0;
	int64_t _pending_handoff_time_us	= 0;

	// Busy time this window. Whatever is left of the window was spent waiting for input.
	int64_t _window_busy_time_us = 0;
};
