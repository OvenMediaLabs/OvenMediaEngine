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

class FilterRescaler : public FilterBase
{
public:
	FilterRescaler();
	~FilterRescaler();

	bool Configure(const std::shared_ptr<MediaTrack> &input_track, const std::shared_ptr<MediaTrack> &output_track) override;
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
};
