//==============================================================================
//
//  Transcode
//
//  Created by Kwon Keuk Han
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include "../../transcoder_encoder.h"

class EncoderHEVCxNILOGAN : public TranscodeEncoder
{
public:
	EncoderHEVCxNILOGAN(const info::Stream &stream_info)
		: TranscodeEncoder(stream_info)
	{
	}

	cmn::MediaCodecId GetCodecID() const noexcept override
	{
		return cmn::MediaCodecId::H265;
	}

	cmn::MediaCodecModuleId GetModuleID() const noexcept
	{
		return cmn::MediaCodecModuleId::NILOGAN;
	}

	cmn::MediaType GetMediaType() const noexcept
	{
		return cmn::MediaType::Video;
	}

	bool IsHWAccel() const noexcept
	{
		return true;
	}

	cmn::AudioSample::Format GetSupportAudioFormat() const noexcept override
	{
		return cmn::AudioSample::Format::None;
	}

	cmn::VideoPixelFormatId GetSupportVideoFormat() const noexcept override 
	{
		return cmn::VideoPixelFormatId::YUV420P;
	}

	cmn::BitstreamFormat GetBitstreamFormat() const noexcept override
	{
		return cmn::BitstreamFormat::H265_ANNEXB;
	}
	
	bool Configure(std::shared_ptr<MediaTrack> context) override;

	bool InitCodec() override;

private:
	bool SetCodecParams() override;	
};
