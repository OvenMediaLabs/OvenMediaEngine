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

	// Weighted average of frame processing time.
	double _weighted_avg_frame_processing_time_us = 0.0;

	// Some devices (e.g. XMA) expand their memory pool while processing the first
	// frame, which is not thread safe. The first frame is processed under the
	// device mutex to prevent allocation failures.
	bool _is_first_frame = true;
};
