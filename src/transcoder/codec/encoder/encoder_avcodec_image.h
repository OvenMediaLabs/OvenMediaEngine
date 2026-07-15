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

// AVCodecImageEncoder handles the software FFmpeg image encoders: JPEG, PNG, WEBP.
class AVCodecImageEncoder : public TranscodeEncoder
{
public:
	AVCodecImageEncoder(const info::Stream &stream_info, cmn::MediaCodecId codec_id)
		: TranscodeEncoder(stream_info), _codec_id(codec_id)
	{
	}

	~AVCodecImageEncoder() override { Uninitialize(); }

	// ----- Codec info -----
	cmn::MediaCodecId GetCodecID() const noexcept override { return _codec_id; }
	cmn::MediaCodecModuleId GetModuleID() const noexcept override { return cmn::MediaCodecModuleId::DEFAULT; }
	cmn::MediaType GetMediaType() const noexcept override { return cmn::MediaType::Video; }
	bool IsHWAccel() const noexcept override { return false; }

	// ----- Supported formats -----
	cmn::AudioSample::Format GetSupportAudioFormat() const noexcept override { return cmn::AudioSample::Format::None; }
	cmn::VideoPixelFormatId GetSupportVideoFormat() const noexcept override
	{
		switch (_codec_id)
		{
			case cmn::MediaCodecId::Png:
				return cmn::VideoPixelFormatId::RGBA;
			case cmn::MediaCodecId::Jpeg:
				return cmn::VideoPixelFormatId::YUVJ420P;
			case cmn::MediaCodecId::Webp:
				return cmn::VideoPixelFormatId::YUV420P;
			default:
				return cmn::VideoPixelFormatId::None;
		}
	}
	cmn::BitstreamFormat GetBitstreamFormat() const noexcept override
	{
		switch (_codec_id)
		{
			case cmn::MediaCodecId::Png:
				return cmn::BitstreamFormat::PNG;
			case cmn::MediaCodecId::Jpeg:
				return cmn::BitstreamFormat::JPEG;
			case cmn::MediaCodecId::Webp:
				return cmn::BitstreamFormat::WEBP;
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
	void SetParamsCommon();
	bool SetParamsJpeg();
	bool SetParamsPng();
	bool SetParamsWebp();

	// ----- Members -----
	cmn::MediaCodecId _codec_id;
	ffmpeg::FFmpegCodec _codec;
	cmn::BitstreamFormat _bitstream_format = cmn::BitstreamFormat::Unknown;
	cmn::PacketType _packet_type = cmn::PacketType::Unknown;
};
