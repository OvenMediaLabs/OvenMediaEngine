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

#include <vector>

// AVCodecImageEncoder handles the software FFmpeg image encoders: JPEG, PNG, WEBP, AVIF.
//
// JPEG/PNG/WEBP flow entirely through FFmpegCodec. AVIF is AV1 video muxed into a
// HEIF (MIAF) container: libaom-av1 emits only the raw bitstream, so each encoded
// AV1 still is deferred-opened to copy the first frame's colour tags into the AV1
// CICP, then wrapped into a one-image AVIF via libavformat's "avif" muxer.
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
			case cmn::MediaCodecId::Avif:
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
			case cmn::MediaCodecId::Avif:
				return cmn::BitstreamFormat::AVIF;
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
	bool SetParamsAvif();

	// AVIF needs a deferred avcodec_open2 (to copy the first frame's colour tags
	// into the AV1 CICP) plus a per-frame container mux, so it drives the codec
	// context directly instead of FFmpegCodec's SendFrame/ReceivePacket helpers.
	EncodeResult SendFrameAvif(const std::shared_ptr<const MediaFrame> &frame);
	EncodeResult ReceivePacketAvif();
	bool OpenAvifCodecWithFrameColor(const AVFrame *frame);
	// Wrap a single AV1 temporal unit into a one-image AVIF in memory. codec_params
	// must carry AV_CODEC_ID_AV1, the picture dimensions and the av1C extradata.
	static bool MuxAvif(const AVCodecParameters *codec_params, AVRational time_base, const void *data, size_t size, std::vector<uint8_t> &out);

	// ----- Members -----
	cmn::MediaCodecId _codec_id;
	ffmpeg::FFmpegCodec _codec;
	cmn::BitstreamFormat _bitstream_format = cmn::BitstreamFormat::Unknown;
	cmn::PacketType _packet_type = cmn::PacketType::Unknown;

	// ----- AVIF-only state -----
	AVPacket *_avif_packet = nullptr;
	AVCodecParameters *_avif_codec_params = nullptr;  // snapshot taken at deferred open
	bool _avif_codec_dead = false;					  // open failed: drop frames so upstream never stalls
};
