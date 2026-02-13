//==============================================================================
//
//  Transcode
//
//  Created by Kwon Keuk Han
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================

#include "filter_fps.h"

#include <base/ovlibrary/ovlibrary.h>

#include "../transcoder_private.h"

FilterFps::FilterFps()
{
	_input_framerate = 0;
	_output_framerate = 0;
	_skip_frames = 0;

	_curr_pts = AV_NOPTS_VALUE;
	_next_pts = AV_NOPTS_VALUE;
	_last_input_pts = AV_NOPTS_VALUE;
	_last_input_scaled_pts = AV_NOPTS_VALUE;

	_stat_ideal_output_frame_count = 0;
	_stat_actual_output_frame_count = 0;
	_stat_input_frame_count = 0;
	_stat_skip_frame_count = 0;
	_stat_duplicate_frame_count = 0;
	_stat_discard_frame_count = 0;
	_last_stat_output_frame_count = 0;
	_stat_output_frame_per_second = 0.0;
}

FilterFps::~FilterFps()
{
	Clear();
}

void FilterFps::Clear()
{
	if(_frames.size() > 0)
	{
		_frames.clear();
	}
	
	_timer.Stop();
}

void FilterFps::SetInputTimebase(cmn::Timebase timebase)
{
	_input_timebase = timebase;
}

void FilterFps::SetInputFrameRate(double framerate)
{
	_input_framerate = framerate;
}

double FilterFps::GetInputFrameRate() const
{
	return _input_framerate;
}

void FilterFps::SetOutputFrameRate(double framerate)
{
	if (_next_pts != AV_NOPTS_VALUE)
	{
		int64_t scaled_next_pts = av_rescale_q_rnd(_next_pts,
												   av_inv_q(av_d2q(_output_framerate, INT_MAX)),
												   av_inv_q(av_d2q(framerate, INT_MAX)),
												   (AVRounding)(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));

		// logtt("Change NextPTS : %lld -> %lld", _next_pts, scaled_next_pts);

		_next_pts = scaled_next_pts;
	}

	_output_framerate = framerate;
}

double FilterFps::GetOutputFrameRate() const
{
	return _output_framerate;
}

void FilterFps::SetSkipFrames(int32_t skip_frames)
{
	_skip_frames = skip_frames;
}

int32_t FilterFps::GetSkipFrames() const
{
	return _skip_frames;
}

bool FilterFps::Push(std::shared_ptr<MediaFrame> media_frame)
{
	_stat_input_frame_count++;

	if (_frames.size() >= 2)
	{
		logtw("FPS filter is full");

		return false;
	}

	// Changed from Timebase PTS to Framerate PTS.
	//  ex) 1/90000 -> 1/30
	//  ex )1/1000 -> 100/2997
	int64_t scaled_pts = av_rescale_q_rnd(media_frame->GetPts(),
										  (AVRational){_input_timebase.GetNum(), _input_timebase.GetDen()},
										  av_inv_q(av_d2q(_output_framerate, INT_MAX)),
										  (AVRounding)(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));

	if ((scaled_pts - _last_input_scaled_pts) != 1 && _last_input_scaled_pts != AV_NOPTS_VALUE)
	{
		// logtt("PTS is not continuous. lastPts(%lld/%lld) -> currPts(%lld/%lld)", _last_input_scaled_pts, _last_input_pts, scaled_pts, media_frame->GetPts());
	}

	_last_input_pts = media_frame->GetPts();
	_last_input_scaled_pts = scaled_pts;

	media_frame->SetPts(scaled_pts);

	if (_next_pts == AV_NOPTS_VALUE)
	{
		_next_pts = media_frame->GetPts();
	}

	_frames.push_back(media_frame);

#if 0
	logtt("Push Frame. PTS(%lld) -> PTS(%lld) (%d/%d) -> (%d/%d)",
		_last_input_pts, _last_input_scaled_pts,
		_input_timebase.GetNum(),  _input_timebase.GetDen(),
		av_inv_q(av_d2q(_output_framerate, INT_MAX)).num, av_inv_q(av_d2q(_output_framerate, INT_MAX)).den);
#endif

	return true;
}

std::shared_ptr<MediaFrame> FilterFps::Pop()
{
	while (_frames.size() >= 2)
	{
		// If the next PTS is less than the PTS of the second frame, 
		// the first frame is discarded.
		if (_frames[1]->GetPts() <= _next_pts)
		{
			_frames.erase(_frames.begin());
			continue;
		}

		_curr_pts = _next_pts;

		// Increase expected PTS to the next frame
		_next_pts++;

		// Skip Frame
		_stat_ideal_output_frame_count++;
		if ((_skip_frames > 0) && (_stat_ideal_output_frame_count % (_skip_frames + 1) != 0))
		{
			_stat_skip_frame_count++;
			continue;
		}

		// Changed from Framerate PTS to Timebase PTS
		int64_t curr_timebase_pts = av_rescale_q_rnd(_curr_pts,
													 av_inv_q(av_d2q(_output_framerate, INT_MAX)),
													 (AVRational){_input_timebase.GetNum(), _input_timebase.GetDen()},
													 (AVRounding)(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));

		// Calculate the PTS of the next frame considering the Skip Frame.
		// Purpose of calculating the Duration of the current frame
		int64_t next_timebase_pts = av_rescale_q_rnd(_next_pts + _skip_frames,
													 av_inv_q(av_d2q(_output_framerate, INT_MAX)),
													 (AVRational){_input_timebase.GetNum(), _input_timebase.GetDen()},
													 (AVRounding)(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));

		auto pop_frame = _frames[0]->CloneFrame(_output_frame_copy_mode == OutputFrameCopyMode::DeepCopy ? true : false);
		pop_frame->SetPts(curr_timebase_pts);

		int64_t duration = next_timebase_pts - curr_timebase_pts;
		pop_frame->SetDuration(duration);

		// Update FPS statistics every second
		if (_timer.IsStart() == false)
		{
			_timer.Start();
		}		
		else if (_timer.IsElapsed(1000))
		{
			int elapsed_time = _timer.Elapsed();
			_timer.Update();

			_stat_actual_output_frame_count = _stat_ideal_output_frame_count - _stat_skip_frame_count;

			// Calculate actual output frames per second
			_stat_output_frame_per_second = (double)(_stat_actual_output_frame_count - _last_stat_output_frame_count) * (1000.0 / (double)elapsed_time);
			
			// logtt("stat: skip_frames:%d, ideal_output_frames:%lld, actual_output_frames:%lld, curr_fps:%.2f, average_fps:%.2f", 
			// 	_skip_frames, _stat_ideal_output_frame_count, _stat_actual_output_frame_count, _stat_output_frame_per_second, 
			// 	_stat_ideal_output_frame_count / (_timer.TotalElapsed() / 1000));

			_last_stat_output_frame_count = _stat_actual_output_frame_count;
		}

		return pop_frame;
	}

	return nullptr;
}

double FilterFps::GetOutputFramesPerSecond() const
{
	return _stat_output_frame_per_second;
}

double FilterFps::GetExpectedOutputFramesPerSecond() const
{
	if (_skip_frames < 0)
	{
		return _output_framerate;
	}

	return _output_framerate / (_skip_frames + 1);
}

ov::String FilterFps::GetStatsString()
{
	ov::String stat;
	stat.Format("InputFrameCount: %lld\n", _stat_input_frame_count);
	stat.Append("OutputFrameCount: %lld\n", _stat_ideal_output_frame_count);
	stat.Append("SkipFrameCount: %lld\n", _stat_skip_frame_count);
	stat.Append("DuplicateFrameCount : %lld\n", _stat_duplicate_frame_count);
	stat.Append("DiscardFrameCount : %lld\n", _stat_discard_frame_count);

	return stat;
}

ov::String FilterFps::GetInfoString()
{
	ov::String info;
	info.Append(ov::String::FormatString("Input Timebase: %d/%d, ", _input_timebase.GetNum(), _input_timebase.GetDen()));
	info.Append(ov::String::FormatString("Input FrameRate: %.2f, ", _input_framerate));
	info.Append(ov::String::FormatString("Output FrameRate: %.2f, ", _output_framerate));
	info.Append(ov::String::FormatString("Skip Frames: %d", _skip_frames));

	return info;
}