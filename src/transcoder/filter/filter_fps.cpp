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
	_skip_frames = SkipFramesDisabled; 

	_curr_pts = kNoPtsValue;
	_next_pts = kNoPtsValue;
	_last_input_pts = kNoPtsValue;
	_last_input_scaled_pts = kNoPtsValue;

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
	if (_next_pts != kNoPtsValue)
	{
		// Rescale _next_pts from the old framerate timebase (1/_output_framerate) to the new one.
		int64_t scaled_next_pts = cmn::Rational::Rescale(
			_next_pts,
			cmn::Rational::FromDouble(_output_framerate, std::numeric_limits<int32_t>::max()).Invert(),
			cmn::Rational::FromDouble(framerate, std::numeric_limits<int32_t>::max()).Invert());

		// logtt("Change NextPTS : %" PRId64 " -> %" PRId64 "", _next_pts, scaled_next_pts);

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
	if(_skip_frames < 0)
	{
		_skip_frames = -1;
	}
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
	int64_t scaled_pts = cmn::Rational::Rescale(
		media_frame->GetPts(),
		cmn::Rational(_input_timebase.GetNum(), _input_timebase.GetDen()),
		cmn::Rational::FromDouble(_output_framerate, std::numeric_limits<int32_t>::max()).Invert());

	if ((scaled_pts - _last_input_scaled_pts) != 1 && _last_input_scaled_pts != kNoPtsValue)
	{
		// logtt("PTS is not continuous. lastPts(%" PRId64 "/%" PRId64 ") -> currPts(%" PRId64 "/%" PRId64 ")", _last_input_scaled_pts, _last_input_pts, scaled_pts, media_frame->GetPts());
	}

	_last_input_pts = media_frame->GetPts();
	_last_input_scaled_pts = scaled_pts;

	media_frame->SetPts(scaled_pts);

	if (_next_pts == kNoPtsValue)
	{
		_next_pts = media_frame->GetPts();
	}

	_frames.push_back(media_frame);

#if 0
	auto output_frame_tb = cmn::Rational::FromDouble(_output_framerate, std::numeric_limits<int32_t>::max()).Invert();
	logtt("Push Frame. PTS(%" PRId64 ") -> PTS(%" PRId64 ") (%d/%d) -> (%d/%d)",
		_last_input_pts, _last_input_scaled_pts,
		_input_timebase.GetNum(),  _input_timebase.GetDen(),
		output_frame_tb.GetNum(), output_frame_tb.GetDen());
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
		if ((_skip_frames > SkipFramesMin) && (_stat_ideal_output_frame_count % (_skip_frames + 1) != 0))
		{
			_stat_skip_frame_count++;
			continue;
		}

		// Changed from Framerate PTS to Timebase PTS
		const auto output_frame_tb = cmn::Rational::FromDouble(_output_framerate, std::numeric_limits<int32_t>::max()).Invert();
		const cmn::Rational input_tb(_input_timebase.GetNum(), _input_timebase.GetDen());

		int64_t curr_timebase_pts = cmn::Rational::Rescale(_curr_pts, output_frame_tb, input_tb);

		// Calculate the next frame's PTS based on _skip_frames.
		int64_t delta = (_skip_frames <= SkipFramesMin) ? 0 : _skip_frames;
		int64_t next_timebase_pts = cmn::Rational::Rescale(_next_pts + delta, output_frame_tb, input_tb);

		auto pop_frame = _frames[0]->CloneFrame(_output_frame_copy_mode == OutputFrameCopyMode::DeepCopy ? true : false);
		pop_frame->SetPts(curr_timebase_pts);

		int64_t duration = next_timebase_pts - curr_timebase_pts;
		pop_frame->SetDuration(duration);

		_stat_actual_output_frame_count = _stat_ideal_output_frame_count - _stat_skip_frame_count;

		// Update FPS statistics every second
		if (_timer.IsStart() == false)
		{
			_timer.Start();
		}		
		else if (_timer.IsElapsed(1000))
		{
			int elapsed_time = _timer.Elapsed();
			_timer.Update();

			// Calculate actual output frames per second (wall-clock based, skipped frames excluded)
			_stat_output_frame_per_second = (double)(_stat_actual_output_frame_count - _last_stat_output_frame_count) * (1000.0 / (double)elapsed_time);
			_last_stat_output_frame_count = _stat_actual_output_frame_count;

			// Calculate actual input frames per second (wall-clock based)
			_stat_input_frame_per_second = (double)(_stat_input_frame_count - _stat_last_input_frame_count) * (1000.0 / (double)elapsed_time);
			_stat_last_input_frame_count = _stat_input_frame_count;
		}

		return pop_frame;
	}

	return nullptr;
}

double FilterFps::GetOutputFramesPerSecond() const
{
	return _stat_output_frame_per_second;
}

double FilterFps::GetInputFramesPerSecond() const
{
	return _stat_input_frame_per_second;
}

double FilterFps::GetExpectedOutputFramesPerSecond() const
{
	if (_skip_frames <= SkipFramesMin)
	{
		return _output_framerate;
	}

	return _output_framerate / (_skip_frames + 1);
}

ov::String FilterFps::GetStatsString()
{
	ov::String stat;
	stat.Format("InputFrameCount: %" PRId64 "\n", _stat_input_frame_count);
	stat.Append("OutputFrameCount: %" PRId64 "\n", _stat_ideal_output_frame_count);
	stat.Append("SkipFrameCount: %" PRId64 "\n", _stat_skip_frame_count);
	stat.Append("DuplicateFrameCount : %" PRId64 "\n", _stat_duplicate_frame_count);
	stat.Append("DiscardFrameCount : %" PRId64 "\n", _stat_discard_frame_count);

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