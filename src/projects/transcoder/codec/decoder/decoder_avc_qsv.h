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

class DecoderAVCxQSV : public TranscodeDecoder
{
public:
	DecoderAVCxQSV(const info::Stream &stream_info)
		: TranscodeDecoder(stream_info)
	{
	}

	cmn::MediaCodecId GetCodecID() const noexcept override
	{
		return cmn::MediaCodecId::H264;
	}

	cmn::MediaCodecModuleId GetModuleID() const noexcept
	{
		return cmn::MediaCodecModuleId::QSV;
	}

	cmn::MediaType GetMediaType() const noexcept
	{
		return cmn::MediaType::Video;
	}

	bool IsHWAccel() const noexcept
	{
		return true;
	}
	
	bool InitCodec();

	void CodecThread() override;
};
