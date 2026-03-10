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

class EncoderFFOPUS : public TranscodeEncoder
{
public:
	EncoderFFOPUS(const info::Stream &stream_info)
		: TranscodeEncoder(stream_info)
	{
	}

	cmn::MediaCodecId GetCodecID() const noexcept override
	{
		return cmn::MediaCodecId::Opus;
	}

	cmn::MediaCodecModuleId GetModuleID() const noexcept override
	{
		return cmn::MediaCodecModuleId::LIBOPUS;
	}

	cmn::MediaType GetMediaType() const noexcept override
	{
		return cmn::MediaType::Audio;
	}

	bool IsHWAccel() const noexcept override
	{
		return false;
	}

	cmn::AudioSample::Format GetSupportAudioFormat() const noexcept override
	{
		return cmn::AudioSample::Format::S16;
	}

	cmn::VideoPixelFormatId GetSupportVideoFormat() const noexcept override
	{
		return cmn::VideoPixelFormatId::None;
	}

	cmn::BitstreamFormat GetBitstreamFormat() const noexcept override
	{
		return cmn::BitstreamFormat::OPUS;
	}
	
	bool Configure(std::shared_ptr<MediaTrack> output_context) override;

	bool InitCodec() override;

private:
	bool SetCodecParams() override;	
};
