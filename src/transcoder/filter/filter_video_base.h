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
	std::vector<std::shared_ptr<MediaFrame>> FlushBuffered() override;
	void InheritContinuity(const FilterBase *previous) override;
	void AddHandoffTime(int64_t elapsed_us) override;

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

	// Consecutive evaluation windows that looked like a bottleneck. One bad window is a
	// hiccup, so the skip level only rises once a window repeats the diagnosis.
	int32_t _skip_frames_bottleneck_count  = 0;
#endif

	void CommitPendingTime(const std::chrono::steady_clock::time_point &start_time);
	void AddProcessingTime(const std::chrono::steady_clock::time_point &start_time);
	void ClearPendingTime();

	// Weighted average of the time spent inside this filter (FPS filter + rescaler graph).
	double _weighted_avg_frame_processing_time_us = 0.0;

	// Weighted average of the time the thread sat blocked handing completed frames to the next stage.
	double _weighted_avg_frame_handoff_time_us	  = 0.0;

	// Processing time that has not been charged to a completed frame yet.
	int64_t _pending_processing_time_us			  = 0;

	// Handoff time that has not been charged to a completed frame yet.
	int64_t _pending_handoff_time_us			  = 0;

	// Everything the thread was busy with during the current evaluation window, both
	// filtering and handing frames off. Whatever is left of the window went to waiting
	// for input, and that idle time is what tells a saturated thread apart from an
	// overloaded one.
	int64_t _window_busy_time_us				  = 0;
};
