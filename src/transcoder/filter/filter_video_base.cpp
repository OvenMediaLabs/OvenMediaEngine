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

#include "../transcoder_gpu.h"
#include "../transcoder_private.h"
#include "../transcoder_stream_internal.h"

bool FilterVideoBase::InitializeFpsFilter()
{
	// Set input parameters
	_fps_filter.SetInputTimebase(_input_track->GetTimeBase());
	_fps_filter.SetInputFrameRate(_input_track->GetFrameRate());

	// Configure skip frames
	int32_t skip_frames_config = _output_track->GetSkipFramesByConfig();
#if _SKIP_FRAMES_ENABLED
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

	if (media_frame != nullptr)
	{
		_fps_filter.Push(media_frame);
	}

	static constexpr double kProcessingTimeEmaAlpha = 0.1;

	while (auto frame = _fps_filter.Pop())
	{
		auto start_time = std::chrono::steady_clock::now();

		if (_is_first_frame && GetOutputTrack()->GetCodecModuleId() == cmn::MediaCodecModuleId::XMA)
		{
			// Some hardware (e.g. Xilinx U30) may expand their memory pool while processing the first frame, which is not thread safe.
			// The first frame is processed under the device mutex to prevent allocation failures.
			ov::ScopedLock first_frame_lock(TranscodeGPU::GetInstance()->GetDeviceMutex());
 			_is_first_frame = false;
			if(SendFrame(frame) == false)
			{
				logte("[%s] Failed to push frame into backend pipeline.", GetLogPrefix().CStr());
				return FilterResult::Error();
			}				

		}
		else
		{
			if (SendFrame(frame) == false)
			{
				logte("[%s] Failed to push frame into backend pipeline.", GetLogPrefix().CStr());
				return FilterResult::Error();
			}
		}

		// Drain every frame produced by this push into the output queue.
		while (auto completed_frame = ReceiveFrame())
		{
			_output_frames.push(std::move(completed_frame));
		}

		if (GetState() == State::ERROR)
		{
			return FilterResult::Error();
		}

		auto elapsed_time_us = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start_time).count();

		// Update the weighted average frame processing time
		// It includes the time taken for filtering + overlay + delivery to the encoder (including waiting time if there is a load on the encoder).
		_weighted_avg_frame_processing_time_us = (_weighted_avg_frame_processing_time_us * (1.0 - kProcessingTimeEmaAlpha)) + (elapsed_time_us * kProcessingTimeEmaAlpha);
	}

#if _SKIP_FRAMES_ENABLED
	UpdateSkipFrames();
#endif

	return FilterResult::NoOutput();
}

FilterResult FilterVideoBase::PopCompletedFrameInternal()
{
	if (_output_frames.empty())
	{
		return FilterResult::NoOutput();
	}

	auto output_frame = std::move(_output_frames.front());
	_output_frames.pop();

	return FilterResult::Ready(std::move(output_frame));
}

#if _SKIP_FRAMES_ENABLED
#define _SKIP_FRAMES_EVALUATION_INTERVAL_MS 	1000	// 1s
#define _SKIP_FRAMES_RECOVERY_HOLD_INTERVAL_MS 	5000	// 5s
#define _SKIP_FRAMES_ENSURE_FPS_MARGIN_RATIO 	0.9f	// 90%

void FilterVideoBase::UpdateSkipFrames()
{
	// Skip frame is disabled.
	if (_skip_frames_conf < 0)
	{
		return;
	}

	// Static skip frames set by the user.
	if (_skip_frames_conf > 0)
	{
		if (_fps_filter.GetSkipFrames() != _skip_frames_conf)
		{
			_fps_filter.SetSkipFrames(_skip_frames_conf);
			logti("[%s] Changed skip frames to user config value: %d", GetLogPrefix().CStr(), _fps_filter.GetSkipFrames());
		}
		return;
	}

	// Automatic skip frame adjustment.

	auto curr_time = ov::Time::GetTimestampInMs();

	if (_skip_frames_last_check_time == 0 || _skip_frames_last_changed_time == 0)
	{
		_skip_frames_last_check_time   = curr_time;
		_skip_frames_last_changed_time = curr_time;
	}

	auto elapsed_check_time = curr_time - _skip_frames_last_check_time;
	auto elapsed_stable_time = curr_time - _skip_frames_last_changed_time;

	// Checking every 1 second is sufficient for skip frame adjustment
	if (elapsed_check_time <= _SKIP_FRAMES_EVALUATION_INTERVAL_MS)
	{
		return;
	}
	_skip_frames_last_check_time = curr_time;

	// Remain for debugging and future improvement for queue-based skip frame adjustment
	// -----------------------------------------------------------------------------
	// double actual_input_fps			   = _fps_filter.GetInputFramesPerSecond();
	// double expected_input_fps		   = _fps_filter.GetInputFrameRate();

	// double expected_output_fps		   = _fps_filter.GetExpectedOutputFramesPerSecond();

	// int64_t queue_waiting_deviation_us = _input_buffer.GetWaitingTimeInUs();
	// double expected_frame_interval_us  = (expected_input_fps > 0.0) ? (1000000.0 / expected_input_fps) : 0.0;
	// bool is_queue_overload			   = (expected_frame_interval_us > 0.0) &&
	// 						 (queue_waiting_deviation_us > expected_frame_interval_us * _SKIP_FRAMES_QUEUE_BACKLOG_RATIO);
	// bool is_queue_stable = (expected_frame_interval_us > 0.0) &&
	// 					   (queue_waiting_deviation_us < expected_frame_interval_us * _SKIP_FRAMES_QUEUE_RECOVERY_RATIO);

	double fixed_output_fps			   = _fps_filter.GetOutputFrameRate();
	double expected_output_fps		   = _fps_filter.GetExpectedOutputFramesPerSecond();
	double actual_output_fps		   = _fps_filter.GetOutputFramesPerSecond();

	if (_weighted_avg_frame_processing_time_us <= 0.0 || fixed_output_fps <= 0.0)
	{
		return;
	}

	// Calculate the maximum possible frames per second.
	double max_frames_per_second = (1000000.0 / _weighted_avg_frame_processing_time_us);
	// To ensure stability, set a margin and use OO% of the calculated maximum FPS.
	double ideal_frames_per_second = max_frames_per_second * _SKIP_FRAMES_ENSURE_FPS_MARGIN_RATIO;

	if (ideal_frames_per_second <= 0.0)
	{
		// If the ideal FPS is not a positive value, skip frame cannot be performed.
		return;
	}

	// Calculate number of skip frames value to match the ideal FPS.
	auto next_skip_frames = static_cast<int32_t>(std::ceil(fixed_output_fps / ideal_frames_per_second - 1.0));
	if (next_skip_frames > fixed_output_fps - 1)
	{
		next_skip_frames = static_cast<int32_t>(std::floor(fixed_output_fps - 1));
	}
	else if (next_skip_frames < FilterFps::SkipFramesMin)
	{
		next_skip_frames = FilterFps::SkipFramesMin;
	}

	ov::String common_log = ov::String::FormatString("Possible FPS: %.2f/%.2f(ideal), Output FPS: %.2f/%.2f/%.2f", max_frames_per_second, ideal_frames_per_second, fixed_output_fps, expected_output_fps, actual_output_fps);

	// Increase skip frames immediately when bottleneck occurs.
	if (_skip_frames < next_skip_frames)
	{
		logtw("[%s] Changed SkipFrames %d -> %d (Bottleneck). %s", GetLogPrefix().CStr(), _skip_frames, next_skip_frames, common_log.CStr());

		_skip_frames = next_skip_frames;
		_fps_filter.SetSkipFrames(_skip_frames);

		_skip_frames_last_changed_time = curr_time;
	}
	// Decrease skip frames slowly when the system is recovering.
	else if ((_skip_frames > next_skip_frames))
	{
		if (elapsed_stable_time > _SKIP_FRAMES_RECOVERY_HOLD_INTERVAL_MS)
		{
			// Decay 20% per step (rate-limited)
			int32_t rate_limited_next = _skip_frames - std::max(1, _skip_frames / 5);
			next_skip_frames = std::max(rate_limited_next, next_skip_frames);
			if (next_skip_frames < FilterFps::SkipFramesMin)
			{
				next_skip_frames = FilterFps::SkipFramesMin;
			}

			logti("[%s] Changed SkipFrames %d -> %d (Recovery). %s", GetLogPrefix().CStr(), _skip_frames, next_skip_frames, common_log.CStr());

			_skip_frames = next_skip_frames;
			_fps_filter.SetSkipFrames(_skip_frames);

			_skip_frames_last_changed_time = curr_time;
		}
		else
		{
			logtt("[%s] Hold SkipFrames %d (Waiting for recovery). %s", GetLogPrefix().CStr(), _skip_frames, common_log.CStr());
		}
	}
	// Keep skip frames unchanged when the system is stable.
	else
	{
		logtt("[%s] Unchanged SkipFrames %d (Stable). %s", GetLogPrefix().CStr(), _skip_frames, common_log.CStr());
	}
}
#endif // _SKIP_FRAMES_ENABLED
