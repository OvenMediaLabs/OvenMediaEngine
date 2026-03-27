//==============================================================================
//
//  Transcode
//
//  Created by Kwon Keuk Han
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================

#pragma once

#include "../transcoder_context.h"
#include "base/mediarouter/media_buffer.h"
#include "base/mediarouter/media_type.h"
#include "filter_base.h"
#include "filter_fps.h"

#define _SKIP_FRAMES_ENABLED 1
#define _SIMULATE_PROCESSING_DELAY_ENABLED 0

class FilterRescaler : public FilterBase
{
public:
	FilterRescaler();
	~FilterRescaler();

	bool Configure() override;
	bool Start() override;
	void Stop() override;

	void WorkerThread();

private:
	bool InitializeFpsFilter();
	bool InitializeSourceFilter();
	bool InitializeFilterDescription();
	bool InitializeSinkFilter();	

	bool PushProcess(std::shared_ptr<MediaFrame> media_frame);
	bool PopProcess(bool is_flush = false);

	bool SetHWContextToFilterIfNeed();	

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

#if _SIMULATE_PROCESSING_DELAY_ENABLED	
	int32_t _simulate_overload = 0;
#endif	

	// Weighted average of frame processing time.
	double _weighted_avg_frame_processing_time_us = 0.0;
};
