//==============================================================================
//
//  Transcode
//
//  Created by Kwon Keuk Han
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include "../../transcoder_decoder.h"

class DecoderVP8 : public TranscodeDecoder
{
public:
	DecoderVP8(const info::Stream &stream_info)
		: TranscodeDecoder(stream_info)
	{
	}

	cmn::MediaCodecId GetCodecID() const noexcept override
	{
		return cmn::MediaCodecId::Vp8;
	}

	cmn::MediaCodecModuleId GetModuleID() const noexcept
	{
		return cmn::MediaCodecModuleId::DEFAULT;
	}

	cmn::MediaType GetMediaType() const noexcept
	{
		return cmn::MediaType::Audio;
	}

	bool IsHWAccel() const noexcept
	{
		return false;
	}

	bool InitCodec();
	void UninitCodec();
	bool ReinitCodecIfNeed();

	void CodecThread() override;
};
