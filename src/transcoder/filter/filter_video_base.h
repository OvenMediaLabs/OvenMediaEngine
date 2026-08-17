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
#include "filter_skipframes_controller.h"

#define _SKIP_FRAMES_ENABLED 1
#define _SIMULATE_PROCESSING_DELAY_ENABLED 0

class FilterVideoBase : public FilterBase
{
public:
	FilterResult ProcessFrameInternal(const std::shared_ptr<MediaFrame> &media_frame) override;
	FilterResult PopCompletedFrameInternal() override;
	std::vector<std::shared_ptr<MediaFrame>> FlushBuffered() override;
	void InheritContinuity(const FilterBase *previous) override;
#if _SKIP_FRAMES_ENABLED
	void AddHandoffTime(int64_t elapsed_us) override;
#endif

protected:
	bool InitializeFpsFilter();

	// Constant FrameRate & SkipFrame Filter
	FilterFps _fps_filter;

#if _SKIP_FRAMES_ENABLED
	// Decides the skip level from the load this filter measures. Fed by the timing calls
	// in PopCompletedFrameInternal(); applied to _fps_filter by UpdateSkipFrames().
	SkipFramesController _skip_frames_controller;

	void UpdateSkipFrames();
#endif
};
