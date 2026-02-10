//==============================================================================
//
//  Type Convert Utilities
//
//  Created by Kwon Keuk Han
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================
#include "./compat.h"
#include <transcoder/transcoder_gpu.h>

namespace ffmpeg
{
	cmn::MediaType compat::ToMediaType(enum AVMediaType codec_type)
	{
		switch (codec_type)
		{
			OV_CASE_RETURN(AVMEDIA_TYPE_UNKNOWN, cmn::MediaType::Unknown);
			OV_CASE_RETURN(AVMEDIA_TYPE_VIDEO, cmn::MediaType::Video);
			OV_CASE_RETURN(AVMEDIA_TYPE_AUDIO, cmn::MediaType::Audio);
			OV_CASE_RETURN(AVMEDIA_TYPE_DATA, cmn::MediaType::Data);
			OV_CASE_RETURN(AVMEDIA_TYPE_SUBTITLE, cmn::MediaType::Subtitle);
			OV_CASE_RETURN(AVMEDIA_TYPE_ATTACHMENT, cmn::MediaType::Attachment);
			OV_CASE_RETURN(AVMEDIA_TYPE_NB, cmn::MediaType::Unknown);
		}
		return cmn::MediaType::Unknown;
	};

	enum AVMediaType compat::ToAVMediaType(cmn::MediaType media_type)
	{
		switch (media_type)
		{
			OV_CASE_RETURN(cmn::MediaType::Unknown, AVMEDIA_TYPE_UNKNOWN);
			OV_CASE_RETURN(cmn::MediaType::Video, AVMEDIA_TYPE_VIDEO);
			OV_CASE_RETURN(cmn::MediaType::Audio, AVMEDIA_TYPE_AUDIO);
			OV_CASE_RETURN(cmn::MediaType::Data, AVMEDIA_TYPE_DATA);
			OV_CASE_RETURN(cmn::MediaType::Subtitle, AVMEDIA_TYPE_SUBTITLE);
			OV_CASE_RETURN(cmn::MediaType::Attachment, AVMEDIA_TYPE_ATTACHMENT);
			OV_CASE_RETURN(cmn::MediaType::Nb, AVMEDIA_TYPE_UNKNOWN);
		}
		return AVMEDIA_TYPE_UNKNOWN;
	};

	cmn::MediaCodecId compat::ToCodecId(enum AVCodecID codec_id)
	{
		switch (codec_id)
		{
			OV_CASE_RETURN(AV_CODEC_ID_H265, cmn::MediaCodecId::H265);
			OV_CASE_RETURN(AV_CODEC_ID_H264, cmn::MediaCodecId::H264);
			OV_CASE_RETURN(AV_CODEC_ID_VP8, cmn::MediaCodecId::Vp8);
			OV_CASE_RETURN(AV_CODEC_ID_VP9, cmn::MediaCodecId::Vp9);
			OV_CASE_RETURN(AV_CODEC_ID_FLV1, cmn::MediaCodecId::Flv);
			OV_CASE_RETURN(AV_CODEC_ID_AAC, cmn::MediaCodecId::Aac);
			OV_CASE_RETURN(AV_CODEC_ID_MP3, cmn::MediaCodecId::Mp3);
			OV_CASE_RETURN(AV_CODEC_ID_OPUS, cmn::MediaCodecId::Opus);
			OV_CASE_RETURN(AV_CODEC_ID_MJPEG, cmn::MediaCodecId::Jpeg);
			OV_CASE_RETURN(AV_CODEC_ID_PNG, cmn::MediaCodecId::Png);
			OV_CASE_RETURN(AV_CODEC_ID_WEBP, cmn::MediaCodecId::Webp);
			default:
				break;
		}

		return cmn::MediaCodecId::None;
	}

	AVCodecID compat::ToAVCodecId(cmn::MediaCodecId codec_id)
	{
		switch (codec_id)
		{
			OV_CASE_RETURN(cmn::MediaCodecId::None, AV_CODEC_ID_NONE);
			OV_CASE_RETURN(cmn::MediaCodecId::H264, AV_CODEC_ID_H264);
			OV_CASE_RETURN(cmn::MediaCodecId::H265, AV_CODEC_ID_H265);
			OV_CASE_RETURN(cmn::MediaCodecId::Vp8, AV_CODEC_ID_VP8);
			OV_CASE_RETURN(cmn::MediaCodecId::Vp9, AV_CODEC_ID_VP9);
			OV_CASE_RETURN(cmn::MediaCodecId::Av1, AV_CODEC_ID_AV1);
			OV_CASE_RETURN(cmn::MediaCodecId::Flv, AV_CODEC_ID_FLV1);
			OV_CASE_RETURN(cmn::MediaCodecId::Aac, AV_CODEC_ID_AAC);
			OV_CASE_RETURN(cmn::MediaCodecId::Mp3, AV_CODEC_ID_MP3);
			OV_CASE_RETURN(cmn::MediaCodecId::Opus, AV_CODEC_ID_OPUS);
			OV_CASE_RETURN(cmn::MediaCodecId::Jpeg, AV_CODEC_ID_MJPEG);
			OV_CASE_RETURN(cmn::MediaCodecId::Png, AV_CODEC_ID_PNG);
			OV_CASE_RETURN(cmn::MediaCodecId::Webp, AV_CODEC_ID_WEBP);
			OV_CASE_RETURN(cmn::MediaCodecId::WebVTT, AV_CODEC_ID_NONE);
			OV_CASE_RETURN(cmn::MediaCodecId::Whisper, AV_CODEC_ID_NONE);
		}

		return AV_CODEC_ID_NONE;
	}

	cmn::AudioSample::Format compat::ToAudioSampleFormat(int format)
	{
		switch (format)
		{
			OV_CASE_RETURN(AV_SAMPLE_FMT_U8, cmn::AudioSample::Format::U8);
			OV_CASE_RETURN(AV_SAMPLE_FMT_S16, cmn::AudioSample::Format::S16);
			OV_CASE_RETURN(AV_SAMPLE_FMT_S32, cmn::AudioSample::Format::S32);
			OV_CASE_RETURN(AV_SAMPLE_FMT_FLT, cmn::AudioSample::Format::Flt);
			OV_CASE_RETURN(AV_SAMPLE_FMT_DBL, cmn::AudioSample::Format::Dbl);
			OV_CASE_RETURN(AV_SAMPLE_FMT_U8P, cmn::AudioSample::Format::U8P);
			OV_CASE_RETURN(AV_SAMPLE_FMT_S16P, cmn::AudioSample::Format::S16P);
			OV_CASE_RETURN(AV_SAMPLE_FMT_S32P, cmn::AudioSample::Format::S32P);
			OV_CASE_RETURN(AV_SAMPLE_FMT_FLTP, cmn::AudioSample::Format::FltP);
			OV_CASE_RETURN(AV_SAMPLE_FMT_DBLP, cmn::AudioSample::Format::DblP);
		}

		return cmn::AudioSample::Format::None;
	}

	int compat::ToAvSampleFormat(cmn::AudioSample::Format format)
	{
		switch (format)
		{
			OV_CASE_RETURN(cmn::AudioSample::Format::None, AV_SAMPLE_FMT_NONE);
			OV_CASE_RETURN(cmn::AudioSample::Format::U8, AV_SAMPLE_FMT_U8);
			OV_CASE_RETURN(cmn::AudioSample::Format::S16, AV_SAMPLE_FMT_S16);
			OV_CASE_RETURN(cmn::AudioSample::Format::S32, AV_SAMPLE_FMT_S32);
			OV_CASE_RETURN(cmn::AudioSample::Format::Flt, AV_SAMPLE_FMT_FLT);
			OV_CASE_RETURN(cmn::AudioSample::Format::Dbl, AV_SAMPLE_FMT_DBL);
			OV_CASE_RETURN(cmn::AudioSample::Format::U8P, AV_SAMPLE_FMT_U8P);
			OV_CASE_RETURN(cmn::AudioSample::Format::S16P, AV_SAMPLE_FMT_S16P);
			OV_CASE_RETURN(cmn::AudioSample::Format::S32P, AV_SAMPLE_FMT_S32P);
			OV_CASE_RETURN(cmn::AudioSample::Format::FltP, AV_SAMPLE_FMT_FLTP);
			OV_CASE_RETURN(cmn::AudioSample::Format::DblP, AV_SAMPLE_FMT_DBLP);

			OV_CASE_RETURN(cmn::AudioSample::Format::Nb, AV_SAMPLE_FMT_NONE);
		}

		return AV_SAMPLE_FMT_NONE;
	}

	int compat::ToAVChannelLayout(cmn::AudioChannel::Layout channel_layout)
	{
		switch (channel_layout)
		{
			OV_CASE_RETURN(cmn::AudioChannel::Layout::LayoutUnknown, AV_CH_LAYOUT_MONO);

			OV_CASE_RETURN(cmn::AudioChannel::Layout::LayoutMono, AV_CH_LAYOUT_MONO);
			OV_CASE_RETURN(cmn::AudioChannel::Layout::LayoutStereo, AV_CH_LAYOUT_STEREO);
			OV_CASE_RETURN(cmn::AudioChannel::Layout::Layout2Point1, AV_CH_LAYOUT_2POINT1);
			OV_CASE_RETURN(cmn::AudioChannel::Layout::Layout21, AV_CH_LAYOUT_2_1);
			OV_CASE_RETURN(cmn::AudioChannel::Layout::LayoutSurround, AV_CH_LAYOUT_SURROUND);
			OV_CASE_RETURN(cmn::AudioChannel::Layout::Layout3Point1, AV_CH_LAYOUT_3POINT1);
			OV_CASE_RETURN(cmn::AudioChannel::Layout::Layout4Point0, AV_CH_LAYOUT_4POINT0);
			OV_CASE_RETURN(cmn::AudioChannel::Layout::Layout4Point1, AV_CH_LAYOUT_4POINT1);
			OV_CASE_RETURN(cmn::AudioChannel::Layout::Layout22, AV_CH_LAYOUT_2_2);
			OV_CASE_RETURN(cmn::AudioChannel::Layout::LayoutQuad, AV_CH_LAYOUT_QUAD);
			OV_CASE_RETURN(cmn::AudioChannel::Layout::Layout5Point0, AV_CH_LAYOUT_QUAD);
			OV_CASE_RETURN(cmn::AudioChannel::Layout::Layout5Point1, AV_CH_LAYOUT_5POINT1);
			OV_CASE_RETURN(cmn::AudioChannel::Layout::Layout5Point0Back, AV_CH_LAYOUT_5POINT0_BACK);
			OV_CASE_RETURN(cmn::AudioChannel::Layout::Layout5Point1Back, AV_CH_LAYOUT_5POINT1_BACK);
			OV_CASE_RETURN(cmn::AudioChannel::Layout::Layout6Point0, AV_CH_LAYOUT_6POINT0);
			OV_CASE_RETURN(cmn::AudioChannel::Layout::Layout6Point0Front, AV_CH_LAYOUT_6POINT0_FRONT);
			OV_CASE_RETURN(cmn::AudioChannel::Layout::LayoutHexagonal, AV_CH_LAYOUT_HEXAGONAL);
			OV_CASE_RETURN(cmn::AudioChannel::Layout::Layout6Point1, AV_CH_LAYOUT_6POINT1);
			OV_CASE_RETURN(cmn::AudioChannel::Layout::Layout6Point1Back, AV_CH_LAYOUT_6POINT1_BACK);
			OV_CASE_RETURN(cmn::AudioChannel::Layout::Layout6Point1Front, AV_CH_LAYOUT_6POINT1_FRONT);
			OV_CASE_RETURN(cmn::AudioChannel::Layout::Layout7Point0, AV_CH_LAYOUT_7POINT0);
			OV_CASE_RETURN(cmn::AudioChannel::Layout::Layout7Point0Front, AV_CH_LAYOUT_7POINT0_FRONT);
			OV_CASE_RETURN(cmn::AudioChannel::Layout::Layout7Point1, AV_CH_LAYOUT_7POINT1);
			OV_CASE_RETURN(cmn::AudioChannel::Layout::Layout7Point1Wide, AV_CH_LAYOUT_7POINT1_WIDE);
			OV_CASE_RETURN(cmn::AudioChannel::Layout::Layout7Point1WideBack, AV_CH_LAYOUT_7POINT1_WIDE_BACK);
			OV_CASE_RETURN(cmn::AudioChannel::Layout::LayoutOctagonal, AV_CH_LAYOUT_OCTAGONAL);
		}

		return AV_CH_LAYOUT_MONO;
	}

	cmn::AudioChannel::Layout compat::ToAudioChannelLayout(int channel_layout)
	{
		switch (channel_layout)
		{
			OV_CASE_RETURN(AV_CH_LAYOUT_MONO, cmn::AudioChannel::Layout::LayoutMono);
			OV_CASE_RETURN(AV_CH_LAYOUT_STEREO, cmn::AudioChannel::Layout::LayoutStereo);
			OV_CASE_RETURN(AV_CH_LAYOUT_2_1, cmn::AudioChannel::Layout::Layout21);
			OV_CASE_RETURN(AV_CH_LAYOUT_SURROUND, cmn::AudioChannel::Layout::LayoutSurround);
			OV_CASE_RETURN(AV_CH_LAYOUT_3POINT1, cmn::AudioChannel::Layout::Layout3Point1);
			OV_CASE_RETURN(AV_CH_LAYOUT_4POINT0, cmn::AudioChannel::Layout::Layout4Point0);
			OV_CASE_RETURN(AV_CH_LAYOUT_4POINT1, cmn::AudioChannel::Layout::Layout4Point1);
			OV_CASE_RETURN(AV_CH_LAYOUT_2_2, cmn::AudioChannel::Layout::Layout22);
			OV_CASE_RETURN(AV_CH_LAYOUT_QUAD, cmn::AudioChannel::Layout::Layout5Point0);
			OV_CASE_RETURN(AV_CH_LAYOUT_5POINT1, cmn::AudioChannel::Layout::Layout5Point1);
			OV_CASE_RETURN(AV_CH_LAYOUT_5POINT1_BACK, cmn::AudioChannel::Layout::Layout5Point1Back);
			OV_CASE_RETURN(AV_CH_LAYOUT_6POINT0, cmn::AudioChannel::Layout::Layout6Point0);
			OV_CASE_RETURN(AV_CH_LAYOUT_6POINT0_FRONT, cmn::AudioChannel::Layout::Layout6Point0Front);
			OV_CASE_RETURN(AV_CH_LAYOUT_HEXAGONAL, cmn::AudioChannel::Layout::LayoutHexagonal);
			OV_CASE_RETURN(AV_CH_LAYOUT_6POINT1, cmn::AudioChannel::Layout::Layout6Point1);
			OV_CASE_RETURN(AV_CH_LAYOUT_6POINT1_BACK, cmn::AudioChannel::Layout::Layout6Point1Back);
			OV_CASE_RETURN(AV_CH_LAYOUT_6POINT1_FRONT, cmn::AudioChannel::Layout::Layout6Point1Front);
			OV_CASE_RETURN(AV_CH_LAYOUT_7POINT0, cmn::AudioChannel::Layout::Layout7Point0);
			OV_CASE_RETURN(AV_CH_LAYOUT_7POINT0_FRONT, cmn::AudioChannel::Layout::Layout7Point0Front);
			OV_CASE_RETURN(AV_CH_LAYOUT_7POINT1, cmn::AudioChannel::Layout::Layout7Point1);
			OV_CASE_RETURN(AV_CH_LAYOUT_7POINT1_WIDE, cmn::AudioChannel::Layout::Layout7Point1Wide);
			OV_CASE_RETURN(AV_CH_LAYOUT_7POINT1_WIDE_BACK, cmn::AudioChannel::Layout::Layout7Point1WideBack);
			OV_CASE_RETURN(AV_CH_LAYOUT_OCTAGONAL, cmn::AudioChannel::Layout::LayoutOctagonal);
		}

		return cmn::AudioChannel::Layout::LayoutUnknown;
	}

	AVPixelFormat compat::ToAVPixelFormat(cmn::VideoPixelFormatId pixel_format)
	{
		switch (pixel_format)
		{
			OV_CASE_RETURN(cmn::VideoPixelFormatId::None, AV_PIX_FMT_NONE);
			OV_CASE_RETURN(cmn::VideoPixelFormatId::YUVJ444P, AV_PIX_FMT_YUVJ444P);
			OV_CASE_RETURN(cmn::VideoPixelFormatId::YUVJ422P, AV_PIX_FMT_YUVJ422P);
			OV_CASE_RETURN(cmn::VideoPixelFormatId::YUVJ420P, AV_PIX_FMT_YUVJ420P);
			OV_CASE_RETURN(cmn::VideoPixelFormatId::YUVA420P, AV_PIX_FMT_YUVA420P);
			OV_CASE_RETURN(cmn::VideoPixelFormatId::YUV444P9, AV_PIX_FMT_YUV444P9);
			OV_CASE_RETURN(cmn::VideoPixelFormatId::YUV444P16, AV_PIX_FMT_YUV444P16);
			OV_CASE_RETURN(cmn::VideoPixelFormatId::YUV444P12, AV_PIX_FMT_YUV444P12);
			OV_CASE_RETURN(cmn::VideoPixelFormatId::YUV444P10, AV_PIX_FMT_YUV444P10);
			OV_CASE_RETURN(cmn::VideoPixelFormatId::YUV444P, AV_PIX_FMT_YUV444P);
			OV_CASE_RETURN(cmn::VideoPixelFormatId::YUV440P12, AV_PIX_FMT_YUV440P12);
			OV_CASE_RETURN(cmn::VideoPixelFormatId::YUV440P10, AV_PIX_FMT_YUV440P10);
			OV_CASE_RETURN(cmn::VideoPixelFormatId::YUV440P, AV_PIX_FMT_YUV440P);
			OV_CASE_RETURN(cmn::VideoPixelFormatId::YUV422P12, AV_PIX_FMT_YUV422P12);
			OV_CASE_RETURN(cmn::VideoPixelFormatId::YUV422P10, AV_PIX_FMT_YUV422P10);
			OV_CASE_RETURN(cmn::VideoPixelFormatId::YUV422P, AV_PIX_FMT_YUV422P);
			OV_CASE_RETURN(cmn::VideoPixelFormatId::YUV420P9, AV_PIX_FMT_YUV420P9);
			OV_CASE_RETURN(cmn::VideoPixelFormatId::YUV420P12, AV_PIX_FMT_YUV420P12);
			OV_CASE_RETURN(cmn::VideoPixelFormatId::YUV420P10, AV_PIX_FMT_YUV420P10);
			OV_CASE_RETURN(cmn::VideoPixelFormatId::YUV420P, AV_PIX_FMT_YUV420P);
			OV_CASE_RETURN(cmn::VideoPixelFormatId::RGB24, AV_PIX_FMT_RGB24);
			OV_CASE_RETURN(cmn::VideoPixelFormatId::P016, AV_PIX_FMT_P016);
			OV_CASE_RETURN(cmn::VideoPixelFormatId::P010, AV_PIX_FMT_P010);
			OV_CASE_RETURN(cmn::VideoPixelFormatId::NV21, AV_PIX_FMT_NV21);
			OV_CASE_RETURN(cmn::VideoPixelFormatId::NV20, AV_PIX_FMT_NV20);
			OV_CASE_RETURN(cmn::VideoPixelFormatId::NV16, AV_PIX_FMT_NV16);
			OV_CASE_RETURN(cmn::VideoPixelFormatId::NV12, AV_PIX_FMT_NV12);
			OV_CASE_RETURN(cmn::VideoPixelFormatId::GRAY8, AV_PIX_FMT_GRAY8);
			OV_CASE_RETURN(cmn::VideoPixelFormatId::GRAY10, AV_PIX_FMT_GRAY10);
			OV_CASE_RETURN(cmn::VideoPixelFormatId::GBRP16, AV_PIX_FMT_GBRP16);
			OV_CASE_RETURN(cmn::VideoPixelFormatId::GBRP12, AV_PIX_FMT_GBRP12);
			OV_CASE_RETURN(cmn::VideoPixelFormatId::GBRP10, AV_PIX_FMT_GBRP10);
			OV_CASE_RETURN(cmn::VideoPixelFormatId::GBRP, AV_PIX_FMT_GBRP;);
			OV_CASE_RETURN(cmn::VideoPixelFormatId::BGR24, AV_PIX_FMT_BGR24);
			OV_CASE_RETURN(cmn::VideoPixelFormatId::BGR0, AV_PIX_FMT_BGR0);
			OV_CASE_RETURN(cmn::VideoPixelFormatId::ARGB, AV_PIX_FMT_ARGB);
			OV_CASE_RETURN(cmn::VideoPixelFormatId::RGBA, AV_PIX_FMT_RGBA;);
			OV_CASE_RETURN(cmn::VideoPixelFormatId::ABGR, AV_PIX_FMT_ABGR);
			OV_CASE_RETURN(cmn::VideoPixelFormatId::BGRA, AV_PIX_FMT_BGRA);
			OV_CASE_RETURN(cmn::VideoPixelFormatId::CUDA, AV_PIX_FMT_CUDA);
#ifdef HWACCELS_XMA_ENABLED
			OV_CASE_RETURN(cmn::VideoPixelFormatId::XVBM_8, AV_PIX_FMT_XVBM_8);
			OV_CASE_RETURN(cmn::VideoPixelFormatId::XVBM_10, AV_PIX_FMT_XVBM_10);
#else	// HWACCELS_XMA_ENABLED
			OV_CASE_RETURN(cmn::VideoPixelFormatId::XVBM_8, AV_PIX_FMT_NONE);
			OV_CASE_RETURN(cmn::VideoPixelFormatId::XVBM_10, AV_PIX_FMT_NONE);
#endif	// HWACCELS_XMA_ENABLED
			OV_CASE_RETURN(cmn::VideoPixelFormatId::NB, AV_PIX_FMT_NONE);
		}

		return AV_PIX_FMT_NONE;
	}

	cmn::VideoPixelFormatId compat::ToVideoPixelFormat(int32_t pixel_format)
	{
		switch (pixel_format)
		{
			OV_CASE_RETURN(AV_PIX_FMT_YUVJ444P, cmn::VideoPixelFormatId::YUVJ444P);
			OV_CASE_RETURN(AV_PIX_FMT_YUVJ422P, cmn::VideoPixelFormatId::YUVJ422P);
			OV_CASE_RETURN(AV_PIX_FMT_YUVJ420P, cmn::VideoPixelFormatId::YUVJ420P);
			OV_CASE_RETURN(AV_PIX_FMT_YUVA420P, cmn::VideoPixelFormatId::YUVA420P);
			OV_CASE_RETURN(AV_PIX_FMT_YUV444P9, cmn::VideoPixelFormatId::YUV444P9);
			OV_CASE_RETURN(AV_PIX_FMT_YUV444P16, cmn::VideoPixelFormatId::YUV444P16);
			OV_CASE_RETURN(AV_PIX_FMT_YUV444P12, cmn::VideoPixelFormatId::YUV444P12);
			OV_CASE_RETURN(AV_PIX_FMT_YUV444P10, cmn::VideoPixelFormatId::YUV444P10);
			OV_CASE_RETURN(AV_PIX_FMT_YUV444P, cmn::VideoPixelFormatId::YUV444P);
			OV_CASE_RETURN(AV_PIX_FMT_YUV440P12, cmn::VideoPixelFormatId::YUV440P12);
			OV_CASE_RETURN(AV_PIX_FMT_YUV440P10, cmn::VideoPixelFormatId::YUV440P10);
			OV_CASE_RETURN(AV_PIX_FMT_YUV440P, cmn::VideoPixelFormatId::YUV440P);
			OV_CASE_RETURN(AV_PIX_FMT_YUV422P12, cmn::VideoPixelFormatId::YUV422P12);
			OV_CASE_RETURN(AV_PIX_FMT_YUV422P10, cmn::VideoPixelFormatId::YUV422P10);
			OV_CASE_RETURN(AV_PIX_FMT_YUV422P, cmn::VideoPixelFormatId::YUV422P);
			OV_CASE_RETURN(AV_PIX_FMT_YUV420P9, cmn::VideoPixelFormatId::YUV420P9);
			OV_CASE_RETURN(AV_PIX_FMT_YUV420P12, cmn::VideoPixelFormatId::YUV420P12);
			OV_CASE_RETURN(AV_PIX_FMT_YUV420P10, cmn::VideoPixelFormatId::YUV420P10);
			OV_CASE_RETURN(AV_PIX_FMT_YUV420P, cmn::VideoPixelFormatId::YUV420P);
			OV_CASE_RETURN(AV_PIX_FMT_RGB24, cmn::VideoPixelFormatId::RGB24);
			OV_CASE_RETURN(AV_PIX_FMT_P016, cmn::VideoPixelFormatId::P016);
			OV_CASE_RETURN(AV_PIX_FMT_P010, cmn::VideoPixelFormatId::P010);
			OV_CASE_RETURN(AV_PIX_FMT_NV21, cmn::VideoPixelFormatId::NV21);
			OV_CASE_RETURN(AV_PIX_FMT_NV20, cmn::VideoPixelFormatId::NV20);
			OV_CASE_RETURN(AV_PIX_FMT_NV16, cmn::VideoPixelFormatId::NV16);
			OV_CASE_RETURN(AV_PIX_FMT_NV12, cmn::VideoPixelFormatId::NV12);
			OV_CASE_RETURN(AV_PIX_FMT_GRAY8, cmn::VideoPixelFormatId::GRAY8);
			OV_CASE_RETURN(AV_PIX_FMT_GRAY10, cmn::VideoPixelFormatId::GRAY10);
			OV_CASE_RETURN(AV_PIX_FMT_GBRP16, cmn::VideoPixelFormatId::GBRP16);
			OV_CASE_RETURN(AV_PIX_FMT_GBRP12, cmn::VideoPixelFormatId::GBRP12);
			OV_CASE_RETURN(AV_PIX_FMT_GBRP10, cmn::VideoPixelFormatId::GBRP10);
			OV_CASE_RETURN(AV_PIX_FMT_GBRP, cmn::VideoPixelFormatId::GBRP);
			OV_CASE_RETURN(AV_PIX_FMT_BGR24, cmn::VideoPixelFormatId::BGR24;);
			OV_CASE_RETURN(AV_PIX_FMT_BGR0, cmn::VideoPixelFormatId::BGR0);
			OV_CASE_RETURN(AV_PIX_FMT_ARGB, cmn::VideoPixelFormatId::ARGB);
			OV_CASE_RETURN(AV_PIX_FMT_RGBA, cmn::VideoPixelFormatId::RGBA);
			OV_CASE_RETURN(AV_PIX_FMT_ABGR, cmn::VideoPixelFormatId::ABGR);
			OV_CASE_RETURN(AV_PIX_FMT_BGRA, cmn::VideoPixelFormatId::BGRA);
			OV_CASE_RETURN(AV_PIX_FMT_CUDA, cmn::VideoPixelFormatId::CUDA);
#ifdef HWACCELS_XMA_ENABLED
			OV_CASE_RETURN(AV_PIX_FMT_XVBM_8, cmn::VideoPixelFormatId::XVBM_8);
			OV_CASE_RETURN(AV_PIX_FMT_XVBM_10, cmn::VideoPixelFormatId::XVBM_10);
#endif	// HWACCELS_XMA_ENABLED
		}

		return cmn::VideoPixelFormatId::None;
	}

	AVPixelFormat compat::GetAVPixelFormatOfHWDevice(cmn::MediaCodecModuleId module_id, cmn::DeviceId gpu_id, bool is_sw_format)
	{
		auto hw_device_ctx = TranscodeGPU::GetInstance()->GetDeviceContext(module_id, gpu_id);
		if (hw_device_ctx == nullptr)
		{
			return AV_PIX_FMT_NONE;
		}

		auto constraints = av_hwdevice_get_hwframe_constraints(hw_device_ctx, nullptr);
		if(constraints == nullptr)
		{
			return AV_PIX_FMT_NONE;
		}

		AVPixelFormat pix_fmt = AV_PIX_FMT_NONE;
		if (is_sw_format == true)
		{
			pix_fmt = *(constraints->valid_sw_formats);
		}
		else {
			pix_fmt = *(constraints->valid_hw_formats);
		}

		av_hwframe_constraints_free(&constraints);

		return pix_fmt;
	}

	ov::String compat::GetAVOptionsString(void *opts)
	{
		ov::String result;

		if (opts == nullptr)
		{
			return result;
		}

		const AVOption *opt = NULL;
		while ((opt = av_opt_next(opts, opt)))
		{
			uint8_t *value = NULL;

			if (av_opt_get(opts, opt->name, 0, &value) == 0)
			{
				result += ov::String::FormatString("%s=%s,", opt->name, value);
				av_free(value);
			}
		}

		return result;
	}
		
}  // namespace ffmpeg
