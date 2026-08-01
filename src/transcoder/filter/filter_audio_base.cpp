//==============================================================================
//
//  Transcode
//
//  Created by Kwon Keuk Han
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================

#include "filter_audio_base.h"

#include <base/ovlibrary/ovlibrary.h>

#include "../transcoder_private.h"

FilterResult FilterAudioBase::ProcessFrameInternal(const std::shared_ptr<MediaFrame> &media_frame)
{
	if (SendFrame(media_frame) == false)
	{
		return FilterResult::Error();
	}

	return FilterResult::NoOutput();
}

FilterResult FilterAudioBase::PopCompletedFrameInternal()
{
	if(GetState() == FilterBase::State::ERROR)
	{
		return FilterResult::Error();
	}

	auto completed_frame = ReceiveFrame();
	if(completed_frame == nullptr)
	{
		return FilterResult::NoOutput();
	}

	return FilterResult::Ready(std::move(completed_frame));
}
