//==============================================================================
//
//  Transcode
//
//  Created by Kwon Keuk Han
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================

#pragma once

#include <chrono>

#include "../media_frame.h"
#include "base/mediarouter/media_buffer.h"
#include "base/mediarouter/media_type.h"
#include "filter_base.h"
#include "filter_fps.h"

#define _SKIP_FRAMES_ENABLED 1
#define _SIMULATE_PROCESSING_DELAY_ENABLED 0

class FilterVideoBase : public FilterBase
{
public:
	FilterResult ProcessFrameInternal(const std::shared_ptr<MediaFrame> &media_frame) override;
	FilterResult PopCompletedFrameInternal() override;

protected:
	bool InitializeFpsFilter();

	// Constant FrameRate & SkipFrame Filter
	FilterFps _fps_filter;

#if _SKIP_FRAMES_ENABLED
	void UpdateSkipFrames();

	int64_t _skip_frames_last_check_time   = 0;
	int64_t _skip_frames_last_changed_time = 0;

	// Set initial Skip Frames
	int32_t _skip_frames_conf			   = -1;
	int32_t _skip_frames				   = -1;
#endif


	int64_t ElapsedTimeInUs(const std::chrono::steady_clock::time_point &start_time) const;
	void UpdateProcessingTimePerFrame(const std::chrono::steady_clock::time_point &start_time);

	// Weighted average of frame processing time.
	double _weighted_avg_frame_processing_time_us = 0.0;

	// Processing time that has not been charged to a completed frame yet.
	int64_t _pending_processing_time_us = 0;
};
