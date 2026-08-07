//==============================================================================
//
//  Transcode
//
//  Created by Kwon Keuk Han
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================

#include "filter_video_base.h"

#include <base/ovlibrary/ovlibrary.h>

#include "../transcoder_private.h"
#include "../transcoder_stream_internal.h"

bool FilterVideoBase::InitializeFpsFilter()
{
	// Set input parameters
	_fps_filter.SetInputTimebase(_input_track->GetTimeBase());
	_fps_filter.SetInputFrameRate(_input_track->GetFrameRate());

	// Configure skip frames
#if _SKIP_FRAMES_ENABLED
	int32_t skip_frames_config = _output_track->GetSkipFramesByConfig();
	int32_t skip_frames = (skip_frames_config >= FilterFps::SkipFramesMin) ? skip_frames_config : FilterFps::SkipFramesDisabled;
	_fps_filter.SetSkipFrames(skip_frames);

	// If skip frames is enabled, maintain input framerate; otherwise use output framerate
	bool is_skip_enabled = (skip_frames >= FilterFps::SkipFramesMin);
	float output_framerate = is_skip_enabled ? _input_track->GetFrameRate() : _output_track->GetFrameRate();
	_fps_filter.SetOutputFrameRate(output_framerate);
#else
	_fps_filter.SetSkipFrames(FilterFps::SkipFramesDisabled);
	_fps_filter.SetOutputFrameRate(_output_track->GetFrameRate());
#endif

	// Set frame copy mode based on resolution
	// Use deep copy when resolutions match to prevent in-place modifications by FFmpeg filter graph
	bool same_resolution = (_input_track->GetResolution() == _output_track->GetResolution());

	auto copy_mode = same_resolution ? FilterFps::OutputFrameCopyMode::DeepCopy
									 : FilterFps::OutputFrameCopyMode::ShallowCopy;
	_fps_filter.SetOutputFrameCopyMode(copy_mode);

	return true;
}

FilterResult FilterVideoBase::ProcessFrameInternal(const std::shared_ptr<MediaFrame> &media_frame)
{
	// If the user does not set the output Framerate, use the recommend framerate
	// Cases where the framerate changes dynamically, such as when using WebRTC, WHIP, or SRTP protocols, were considered.
	// It is similar to maintaining the original frame rate.
	if (_output_track->GetFrameRateByConfig() == 0.0f)
	{
		auto recommended_output_framerate = TranscoderStreamInternal::MeasurementToRecommendFramerate(_input_track->GetFrameRate());
		if (_fps_filter.GetOutputFrameRate() != recommended_output_framerate)
		{
			logtd("[%s] Change output framerate. Input: %.2ffps, Output: %.2f -> %.2ffps", GetLogPrefix().CStr(), _input_track->GetFrameRate(), _fps_filter.GetOutputFrameRate(), recommended_output_framerate);
			_fps_filter.SetOutputFrameRate(recommended_output_framerate);
		}
	}

	if (media_frame == nullptr)
	{
		return FilterResult::Error("Received frame is null");
	}

#if _SKIP_FRAMES_ENABLED
	auto start_time = std::chrono::steady_clock::now();
#endif

	if (_fps_filter.Push(media_frame) == false)
	{
		logtw("[%s] Dropped a frame because the FPS filter is full.", GetLogPrefix().CStr());
	}

#if _SKIP_FRAMES_ENABLED
	// The push runs on the filter thread like everything else, so it counts toward how
	// busy the thread was this window.
	_skip_frames_controller.AddBusyTime(ov::Clock::GetElapsedMicroSecondsFromNow(start_time));
#endif

	return FilterResult::NoOutput();
}

FilterResult FilterVideoBase::PopCompletedFrameInternal()
{
	if (GetState() == FilterBase::State::ERROR)
	{
#if _SKIP_FRAMES_ENABLED
		_skip_frames_controller.DiscardPending();
#endif

		return FilterResult::Error("The filter is in the error state");
	}

#if _SKIP_FRAMES_ENABLED
	auto start_time = std::chrono::steady_clock::now();
#endif

	while (true)
	{
		auto completed_frame = ReceiveFrame();
		if (completed_frame != nullptr)
		{
#if _SKIP_FRAMES_ENABLED
			_skip_frames_controller.CommitFrame(ov::Clock::GetElapsedMicroSecondsFromNow(start_time));
#endif

			return FilterResult::Ready(std::move(completed_frame));
		}

		if (GetState() == FilterBase::State::ERROR)
		{
#if _SKIP_FRAMES_ENABLED
			_skip_frames_controller.DiscardPending();
#endif

			return FilterResult::Error("The backend rescaler has failed");
		}

		// Drain FPS filter to get completed frames.
		auto frame = _fps_filter.Pop();
		if (frame == nullptr)
		{
#if _SKIP_FRAMES_ENABLED
			_skip_frames_controller.AddProcessingTime(ov::Clock::GetElapsedMicroSecondsFromNow(start_time));

			// Update skip frames based on the current processing time and framerate.
			UpdateSkipFrames();
#endif

			return FilterResult::NoOutput();
		}

		if (SendFrame(frame) == false)
		{
			// A fatal failure is carried in the filter state and reported by the check at the
			// top of the next round. Anything else only dropped this frame.
			logtw("[%s] Dropped a frame that could not be pushed into the backend rescaler.", GetLogPrefix().CStr());
		}
	}
}

std::vector<std::shared_ptr<MediaFrame>> FilterVideoBase::FlushBuffered()
{
	std::vector<std::shared_ptr<MediaFrame>> flushed_frames;

	// Push the frames parked in the FPS filter through the rescaler graph
	while (true)
	{
		auto parked_frame = _fps_filter.Flush();
		if (parked_frame == nullptr)
		{
			break;
		}

		if (SendFrame(parked_frame) == false)
		{
			logtw("[%s] Dropped a parked frame that could not be pushed into the backend rescaler.", GetLogPrefix().CStr());
		}
	}

	// Drain whatever the graph completes
	while (true)
	{
		auto completed_frame = ReceiveFrame();
		if (completed_frame == nullptr)
		{
			break;
		}

		flushed_frames.push_back(std::move(completed_frame));
	}

	return flushed_frames;
}

void FilterVideoBase::InheritContinuity(const FilterBase *previous)
{
	auto previous_video = dynamic_cast<const FilterVideoBase *>(previous);
	if (previous_video == nullptr)
	{
		return;
	}
	// The slot position only transfers within the same output cadence
	if (_fps_filter.GetOutputFrameRate() != previous_video->_fps_filter.GetOutputFrameRate())
	{
		return;
	}

	// A frame lost around the swap (e.g. still inside the decoder at a format
	// change) leaves a slot hole no instance can see on its own; carrying the
	// slot position lets the new filter refill it
	_fps_filter.SetContinuationPts(previous_video->_fps_filter.GetNextPts());

#if _SKIP_FRAMES_ENABLED
	// The encoder does not empty out because this filter was replaced, so the load the
	// outgoing controller measured still describes the pipeline.
	_skip_frames_controller.InheritFrom(previous_video->_skip_frames_controller);

	_fps_filter.SetSkipFrames(_skip_frames_controller.GetSkipFrames());
#endif
}

#if _SKIP_FRAMES_ENABLED
void FilterVideoBase::AddHandoffTime(int64_t elapsed_us)
{
	// Called on the filter thread, so no synchronization is needed.
	_skip_frames_controller.AddHandoffTime(elapsed_us);
}

void FilterVideoBase::UpdateSkipFrames()
{
	// Skip frame is disabled.
	if (_skip_frames_controller.IsEnabled() == false)
	{
		return;
	}

	// Static skip frames set by the user.
	if (_skip_frames_controller.IsAutomatic() == false)
	{
		auto configured = _skip_frames_controller.GetConfiguredSkipFrames();
		if (_fps_filter.GetSkipFrames() != configured)
		{
			_fps_filter.SetSkipFrames(configured);
			logti("[%s] SkipFrames %d, fixed by config.", GetLogPrefix().CStr(), _fps_filter.GetSkipFrames());
		}
		return;
	}

	// Automatic skip frame adjustment. The controller owns the decision; this applies it
	// and reports it, because the log prefix and the FPS filter live here.
	SkipFramesController::Observation observation;
	observation.expected_input_fps	= _fps_filter.GetInputFrameRate();
	observation.actual_input_fps	= _fps_filter.GetInputFramesPerSecond();
	observation.cadence_fps	= _fps_filter.GetOutputFrameRate();
	observation.expected_output_fps	= _fps_filter.GetExpectedOutputFramesPerSecond();
	observation.actual_output_fps	= _fps_filter.GetOutputFramesPerSecond();

	// Monotonic, not wall clock: the busy time this is divided against comes from
	// steady_clock, and a system clock step would make the two disagree - stalling the
	// evaluation while busy time piles up, then charging all of it to one window.
	auto result = _skip_frames_controller.Evaluate(ov::Time::GetMonotonicTimestamp(), observation);
	if (result.has_value() == false)
	{
		return;
	}

	auto previous_skip_frames = _fps_filter.GetSkipFrames();

	// All four lines lead with "SkipFrames <level>" so one grep follows a track through
	// every state. Warn only where output actually degrades.
	switch (result->decision)
	{
		case SkipFramesController::Decision::Bottleneck:
			logtw("[%s] SkipFrames %d -> %d, pipeline cannot keep up. %s", GetLogPrefix().CStr(), previous_skip_frames, result->skip_frames, result->metrics.CStr());
			_fps_filter.SetSkipFrames(result->skip_frames);
			break;

		case SkipFramesController::Decision::Recovery:
			logti("[%s] SkipFrames %d -> %d, capacity came back. %s", GetLogPrefix().CStr(), previous_skip_frames, result->skip_frames, result->metrics.CStr());
			_fps_filter.SetSkipFrames(result->skip_frames);
			break;

		// Both no-ops, and they fire once per second per track. Trace, so a debug-level
		// deployment is not buried under a line per track per second.
		case SkipFramesController::Decision::HoldRecovery:
			logtt("[%s] SkipFrames %d, waiting out the recovery hold. %s", GetLogPrefix().CStr(), result->skip_frames, result->metrics.CStr());
			break;

		case SkipFramesController::Decision::Unchanged:
			logtt("[%s] SkipFrames %d, steady. %s", GetLogPrefix().CStr(), result->skip_frames, result->metrics.CStr());
			break;
	}
}
#endif // _SKIP_FRAMES_ENABLED
