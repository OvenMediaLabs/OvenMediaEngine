//==============================================================================
//
//  Transcode
//
//  Created by Kwon Keuk Han
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include "decoder_mp3.h"

class DecoderMP2 : public DecoderMP3
{
public:
	DecoderMP2(const info::Stream &stream_info)
		: DecoderMP3(stream_info)
	{
	}

	cmn::MediaCodecId GetCodecID() const noexcept override
	{
		return cmn::MediaCodecId::Mp2;
	}
};
