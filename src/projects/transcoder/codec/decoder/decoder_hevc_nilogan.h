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

class DecoderHEVCxNILOGAN : public TranscodeDecoder
{
public:
	DecoderHEVCxNILOGAN(const info::Stream &stream_info)
		: TranscodeDecoder(stream_info)
	{
	}

	cmn::MediaCodecId GetCodecID() const noexcept override
	{
		return cmn::MediaCodecId::H265;
	}

	cmn::MediaCodecModuleId GetModuleID() const noexcept override
	{
		return cmn::MediaCodecModuleId::NILOGAN;
	}

	cmn::MediaType GetMediaType() const noexcept override
	{
		return cmn::MediaType::Video;
	}

	bool IsHWAccel() const noexcept override
	{
		return true;
	}

	bool InitCodec() override;

	void CodecThread() override;
};
