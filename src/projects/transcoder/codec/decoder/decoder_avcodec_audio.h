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

// Software (FFmpeg/libavcodec) audio decoder (AAC/MP3/OPUS/MP2). 
class AVCodecAudioDecoder : public TranscodeDecoder
{
public:
	AVCodecAudioDecoder(const info::Stream &stream_info, cmn::MediaCodecId codec_id)
		: TranscodeDecoder(stream_info), _codec_id(codec_id)
	{
	}

	~AVCodecAudioDecoder() override
	{
		Stop();
		Uninitialize();
	}

	// ----- Codec info -----
	cmn::MediaCodecId GetCodecID() const noexcept override { return _codec_id; }
	cmn::MediaCodecModuleId GetModuleID() const noexcept override { return cmn::MediaCodecModuleId::DEFAULT; }
	cmn::MediaType GetMediaType() const noexcept override { return cmn::MediaType::Audio; }
	bool IsHWAccel() const noexcept override { return false; }

	// ----- Decoder interface -----
	bool Initialize() override;

private:
	// ----- Decoder interface -----
	std::shared_ptr<MediaPacket> GetFramedPacket() override;
	DecodeResult SendPacket(const std::shared_ptr<MediaPacket> &packet) override;
	DecodeResult ReceiveFrame() override;
	void Uninitialize() override;

	// ----- Members -----
	cmn::MediaCodecId _codec_id;
	ffmpeg::FFmpegCodec _codec;
	ffmpeg::FFmpegBitstreamFramer _framer;
	PaddedAlignedBuffer _framing_buffer;
	bool _change_format = false;
	
	int64_t _first_pkt_pts = INT64_MIN;
	int64_t _last_pkt_pts = INT64_MIN;
	int64_t _last_pkt_duration = 0;
};
