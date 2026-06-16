//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Keukhan
//  Copyright (c) 2026 OvenMediaLabs. All rights reserved.
//
//==============================================================================
#pragma once

#include "../../transcoder_decoder.h"
#include <modules/ffmpeg/ffmpeg_codec.h>
#include <modules/ffmpeg/ffmpeg_bitstream_framer.h>
#include <transcoder/padded_aligned_buffer.h>

// Software (FFmpeg/libavcodec) video decoder (H264/H265/VP8). Selected for the DEFAULT module.
class AVCodecVideoDecoder : public TranscodeDecoder
{
public:
	AVCodecVideoDecoder(const info::Stream &stream_info, cmn::MediaCodecId codec_id)
		: TranscodeDecoder(stream_info), _codec_id(codec_id)
	{
	}

	~AVCodecVideoDecoder() override
	{
		Stop();
		Uninitialize();
	}

	// ----- Codec info -----
	cmn::MediaCodecId GetCodecID() const noexcept override { return _codec_id; }
	cmn::MediaCodecModuleId GetModuleID() const noexcept override { return cmn::MediaCodecModuleId::DEFAULT; }
	cmn::MediaType GetMediaType() const noexcept override { return cmn::MediaType::Video; }
	bool IsHWAccel() const noexcept override { return false; }

	// ----- Decoder interface -----
	bool Initialize() override;

private:
	// ----- Decoder interface -----
	std::shared_ptr<MediaPacket> GetFramedPacket() override;
	DecodeResult SendPacket(const std::shared_ptr<MediaPacket> &packet) override;
	DecodeResult ReceiveFrame() override;
	void Uninitialize() override;

	// ----- Internal helpers -----
	bool ReinitCodecIfNeed();

	// ----- Members -----
	cmn::MediaCodecId _codec_id;
	ffmpeg::FFmpegCodec _codec;
	ffmpeg::FFmpegBitstreamFramer _framer;
	PaddedAlignedBuffer _framing_buffer;
	bool _change_format = false;
};
