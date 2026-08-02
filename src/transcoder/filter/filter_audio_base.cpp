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
	if (media_frame == nullptr)
	{
		return FilterResult::Error("Received frame is null");
	}

	if (SendFrame(media_frame) == false)
	{
		logtw("[%s] Dropped a frame that could not be pushed into the backend resampler.", GetLogPrefix().CStr());
	}

	return FilterResult::NoOutput();
}

FilterResult FilterAudioBase::PopCompletedFrameInternal()
{
	if (GetState() == FilterBase::State::ERROR)
	{
		return FilterResult::Error("The filter is in the error state");
	}

	auto completed_frame = ReceiveFrame();
	if (completed_frame == nullptr)
	{
		// ReceiveFrame() also returns nullptr when it fails, so the state decides whether
		// this is an empty pipeline or an error that has to be reported right away.
		if (GetState() == FilterBase::State::ERROR)
		{
			return FilterResult::Error("Failed to receive frame from backend resampler");
		}

		return FilterResult::NoOutput();
	}

	return FilterResult::Ready(std::move(completed_frame));
}
