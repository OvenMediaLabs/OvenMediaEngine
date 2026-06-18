//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Keukhan
//  Copyright (c) 2026 OvenMediaLabs. All rights reserved.
//
//==============================================================================
#pragma once

#include "../../transcoder_encoder.h"
#include <modules/ffmpeg/ffmpeg_codec.h>

// AVCodecAudioEncoder handles the software FFmpeg audio encoders: AAC and OPUS (libopus).
// Self-contained: owns its full encode pipeline.
class AVCodecAudioEncoder : public TranscodeEncoder
{
public:
	AVCodecAudioEncoder(const info::Stream &stream_info, cmn::MediaCodecId codec_id)
		: TranscodeEncoder(stream_info), _codec_id(codec_id)
	{
	}

	~AVCodecAudioEncoder() override { Uninitialize(); }

	// ----- Codec info -----
	cmn::MediaCodecId GetCodecID() const noexcept override { return _codec_id; }
	cmn::MediaCodecModuleId GetModuleID() const noexcept override
	{
		return (_codec_id == cmn::MediaCodecId::Opus) ? cmn::MediaCodecModuleId::LIBOPUS : cmn::MediaCodecModuleId::FDKAAC;
	}
	cmn::MediaType GetMediaType() const noexcept override { return cmn::MediaType::Audio; }
	bool IsHWAccel() const noexcept override { return false; }

	// ----- Supported formats -----
	cmn::AudioSample::Format GetSupportAudioFormat() const noexcept override { return cmn::AudioSample::Format::S16; }
	cmn::VideoPixelFormatId GetSupportVideoFormat() const noexcept override { return cmn::VideoPixelFormatId::None; }
	cmn::BitstreamFormat GetBitstreamFormat() const noexcept override
	{
		switch (_codec_id)
		{
			case cmn::MediaCodecId::Opus:
				return cmn::BitstreamFormat::OPUS;
			case cmn::MediaCodecId::Aac:
				return cmn::BitstreamFormat::AAC_ADTS;
			default:
				return cmn::BitstreamFormat::Unknown;
		}
	}

protected:
	// ----- Encoder interface -----
	bool Initialize() override;
	void Uninitialize() override;
	EncodeResult SendFrame(const std::shared_ptr<const MediaFrame> &frame, bool force_keyframe) override;
	EncodeResult ReceivePacket() override;

private:
	// ----- Internal helpers -----
	bool OpenCodec();
	bool SetParamsAac();
	bool SetParamsOpus();

	// ----- Members -----
	cmn::MediaCodecId _codec_id;
	ffmpeg::FFmpegCodec _codec;
	cmn::BitstreamFormat _bitstream_format = cmn::BitstreamFormat::Unknown;
	cmn::PacketType _packet_type = cmn::PacketType::Unknown;
};
