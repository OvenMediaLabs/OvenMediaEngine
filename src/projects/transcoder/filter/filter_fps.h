//==============================================================================
//
//  Transcode
//
//  Created by Kwon Keuk Han
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================

#pragma once

#include "../media_frame.h"
#include "base/mediarouter/media_buffer.h"
#include "base/mediarouter/media_type.h"
#include "filter_base.h"
#include "base/ovlibrary/ovlibrary.h"

class FilterFps 
{
public:
	FilterFps();
	~FilterFps();

	void Clear();

	void SetInputTimebase(cmn::Timebase timebase);
	void SetInputFrameRate(double framerate);
	double GetInputFrameRate() const;
	void SetOutputFrameRate(double framerate);
	double GetOutputFrameRate() const;

	void SetSkipFrames(int32_t skip_frames);
	int32_t GetSkipFrames() const;
	void SetMaximumDupulicateFrames(int32_t max_dupulicate_frames);
	bool Push(std::shared_ptr<MediaFrame> media_frame);
	std::shared_ptr<MediaFrame> Pop();

	double GetOutputFramesPerSecond() const;
	double GetExpectedOutputFramesPerSecond() const;
	double GetInputFramesPerSecond() const;

	ov::String GetStatsString();
	ov::String GetInfoString();

public:
	enum class OutputFrameCopyMode
	{
		ShallowCopy = 0,
		DeepCopy	= 2
	};

	void SetOutputFrameCopyMode(OutputFrameCopyMode mode)
	{
		_output_frame_copy_mode = mode;
	}


	static const int32_t SkipFramesDisabled = -1;
	static const int32_t SkipFramesMin		= 0;

private:
	static constexpr int64_t kNoPtsValue = INT64_MIN;

	cmn::Timebase _input_timebase;
	double _input_framerate;
	double _output_framerate;

	// Maximum number of duplicate frames. 
	// If the number of duplicate frames exceeds this value, the frame is discarded.
	// Prevents the frame from being duplicated indefinitely.
	int32_t _max_dupulicate_frames = 5;

	// Skip frames configuration
	// -1: disabled
	// >=0: the number of frames to skip (e.g., 0 means no skip, 1 means skip 1 frame after every output frame)
	int32_t _skip_frames = SkipFramesDisabled;
	
	// Buffer for storing frames
	std::vector<std::shared_ptr<MediaFrame>> _frames;

	// Output frame copy mode
	OutputFrameCopyMode _output_frame_copy_mode = OutputFrameCopyMode::DeepCopy;

	// The next PTS to be output
	int64_t _curr_pts;
	int64_t _next_pts;

	int64_t _last_input_pts					= kNoPtsValue;
	int64_t _last_input_scaled_pts			= kNoPtsValue;

	int64_t _stat_input_frame_count			= 0;
	int64_t _stat_ideal_output_frame_count	= 0;
	int64_t _stat_actual_output_frame_count = 0;
	int64_t _stat_skip_frame_count			= 0;
	int64_t _stat_duplicate_frame_count		= 0;
	int64_t _stat_discard_frame_count		= 0;

	// Wall-clock based actual output frame rate (frames popped per second, skipped frames excluded)
	int64_t _last_stat_output_frame_count	= 0;
	double _stat_output_frame_per_second	= 0.0f;

	// Wall-clock based actual input frame rate (frames pushed per second)
	int64_t _stat_last_input_frame_count	= 0;
	double  _stat_input_frame_per_second	= 0.0;

	ov::StopWatch _timer;

};
