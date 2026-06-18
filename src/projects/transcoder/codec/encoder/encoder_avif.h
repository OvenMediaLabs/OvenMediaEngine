//==============================================================================
//
//  Transcode
//
//  Copyright (c) 2026 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <vector>

#include "../../transcoder_encoder.h"

// AVIF thumbnail encoder.
//
// AVIF is AV1 video data muxed into a HEIF (MIAF) container. libaom-av1 emits
// only the raw bitstream, so each encoded AV1 still is wrapped into a one-image
// AVIF using libavformat's "avif" muxer over an in-memory AVIO buffer.
//
// avcodec_open2 is deferred to the first frame so the codec context can take that
// frame's colour tags verbatim: the AVIF CICP then signals exactly what the
// rescaler produced, with no hardcoded fallbacks.
//
// Unlike JPEG/PNG/WEBP (consolidated into AVCodecImageEncoder via FFmpegCodec),
// AVIF needs a deferred open plus a per-frame container mux, so it drives a raw
// AVCodecContext directly and implements SendFrame/ReceivePacket.
class EncoderAVIF : public TranscodeEncoder
{
public:
	EncoderAVIF(const info::Stream &stream_info)
		: TranscodeEncoder(stream_info)
	{
	}

	~EncoderAVIF() override;

	cmn::MediaCodecId GetCodecID() const noexcept override { return cmn::MediaCodecId::Avif; }
	cmn::MediaCodecModuleId GetModuleID() const noexcept override { return cmn::MediaCodecModuleId::DEFAULT; }
	cmn::MediaType GetMediaType() const noexcept override { return cmn::MediaType::Video; }
	bool IsHWAccel() const noexcept override { return false; }
	cmn::AudioSample::Format GetSupportAudioFormat() const noexcept override { return cmn::AudioSample::Format::None; }
	cmn::VideoPixelFormatId GetSupportVideoFormat() const noexcept override
	{
		// <ChromaSampling>444</ChromaSampling> disables chroma subsampling (AV1 High profile).
		if (_track != nullptr && _track->GetChromaSampling() == "444")
		{
			return cmn::VideoPixelFormatId::YUV444P;
		}
		return cmn::VideoPixelFormatId::YUV420P;
	}
	cmn::BitstreamFormat GetBitstreamFormat() const noexcept override { return cmn::BitstreamFormat::AVIF; }

	// Wrap a single AV1 temporal unit into a one-image AVIF in memory. codec_params
	// must carry AV_CODEC_ID_AV1, the picture dimensions and the av1C extradata; the
	// muxer back-patches HEIF box sizes, which the dynamic AVIO buffer supports.
	static bool MuxAvif(const AVCodecParameters *codec_params, AVRational time_base, const void *data, size_t size, std::vector<uint8_t> &out);

protected:
	bool Initialize() override;
	void Uninitialize() override;
	EncodeResult SendFrame(const std::shared_ptr<const MediaFrame> &frame, bool force_keyframe) override;
	EncodeResult ReceivePacket() override;

private:
	bool SetCodecParams();

	// Open the codec with the colour tags of the given frame, then snapshot the
	// resulting codec parameters (incl. the av1C extradata) for muxing.
	bool OpenCodecWithFrameColor(const AVFrame *frame);

	AVCodecContext *_codec_context = nullptr;
	AVPacket *_packet = nullptr;
	AVCodecParameters *_codec_params = nullptr;

	// Set when the codec failed to open; input frames are then dropped so the
	// upstream never stalls.
	bool _codec_dead = false;
};
