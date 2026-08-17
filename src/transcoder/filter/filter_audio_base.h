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

class FilterAudioBase : public FilterBase
{
public:
	FilterResult ProcessFrameInternal(const std::shared_ptr<MediaFrame> &media_frame) override;
	FilterResult PopCompletedFrameInternal() override;
};
