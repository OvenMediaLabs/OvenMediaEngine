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

	// Drain every frame produced by this push into the output queue.
	while (auto completed_frame = ReceiveFrame())
	{
		_output_frames.push(std::move(completed_frame));
	}

	if (GetState() == State::ERROR)
	{
		return FilterResult::Error();
	}

	return FilterResult::NoOutput();
}

FilterResult FilterAudioBase::PopCompletedFrameInternal()
{
	if (_output_frames.empty())
	{
		return FilterResult::NoOutput();
	}

	auto output_frame = std::move(_output_frames.front());
	_output_frames.pop();

	return FilterResult::Ready(std::move(output_frame));
}
