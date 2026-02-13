//==============================================================================
//
//  Machine 
//
//  Created by Kwon Keuk Han
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <whisper.h>
#include <base/provider/stream.h>
#include "../../transcoder_encoder.h"

class EncoderWhisper : public TranscodeEncoder
{
public:
	EncoderWhisper(const info::Stream &stream_info);
	~EncoderWhisper() override;
	
	cmn::MediaCodecId GetCodecID() const noexcept override
	{
		return cmn::MediaCodecId::Whisper;
	}

	cmn::MediaCodecModuleId GetModuleID() const noexcept
	{
		return cmn::MediaCodecModuleId::NVENC;
	}

	cmn::MediaType GetMediaType() const noexcept
	{
		return cmn::MediaType::Audio;
	}

	bool IsHWAccel() const noexcept
	{
		return true;
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
		return cmn::BitstreamFormat::WebVTT;
	}
	
	bool Configure(std::shared_ptr<MediaTrack> context) override;
	bool InitCodec() override;
	void CodecThread() override;

private:
	bool SetCodecParams() override;	
	ov::String ToTimeString(int64_t ten_ms);
	bool SendVttToProvider(const ov::String &text);
	bool SendLangDetectionEvent(const ov::String &label, const ov::String &language);

	int32_t _step_ms = 2000;
	int32_t _length_ms = 8000;
	int32_t _keep_ms = 100;
	struct whisper_context * _whisper_ctx = nullptr;

	int32_t _n_samples_step = 0;
	int32_t _n_samples_length = 0;
	int32_t _n_samples_keep = 0;

	ov::String _source_language = "auto";
	bool _translate = false;
	ov::String _output_track_label;

	std::shared_ptr<pvd::Stream> _parent_stream = nullptr;
};
