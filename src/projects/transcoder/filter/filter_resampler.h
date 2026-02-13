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

class FilterResampler : public FilterBase
{
public:
	FilterResampler();
	~FilterResampler();

	bool Configure(const std::shared_ptr<MediaTrack> &input_track, const std::shared_ptr<MediaTrack> &output_track) override;
	bool Start() override;
	void Stop() override;

	void WorkerThread();

private:
	bool InitializeSourceFilter();
	bool InitializeFilterDescription();
	bool InitializeSinkFilter();
	
};
