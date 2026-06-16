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
#include "filter_audio_base.h"

#include <modules/ffmpeg/compat.h>
#include <modules/ffmpeg/ffmpeg_filter_graph.h>

class FilterLavfiResampler : public FilterAudioBase
{
public:
	FilterLavfiResampler() = default;
	~FilterLavfiResampler() override;

	// ----- Filter interface -----
	bool Initialize() override;
	void Uninitialize() override;
	bool SendFrame(std::shared_ptr<MediaFrame> media_frame) override;
	std::shared_ptr<MediaFrame> ReceiveFrame() override;

private:
	// ----- Internal helpers (FFmpeg avfilter graph) -----
	bool InitializeSourceFilter();
	bool InitializeFilterDescription();
	bool InitializeSinkFilter();

	// ----- Members -----
	ffmpeg::FFmpegFilterGraph _graph;
};
