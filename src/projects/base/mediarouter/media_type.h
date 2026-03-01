//==============================================================================
//
//  Transcode
//
//  Created by Kwon Keuk Han
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <base/ovlibrary/ovlibrary.h>

namespace cmn
{
	enum class MediaRouterStreamType : int8_t
	{
		UNKNOWN = -1,
		INBOUND,
		OUTBOUND
	};

	enum class MediaType : int8_t
	{
		Unknown = -1,
		Video	= 0,
		Audio,
		Data,
		Subtitle,
		Attachment,
		Nb
	};

	enum class BitstreamFormat : int8_t
	{
		Unknown	  = -1,
		H264_AVCC = 0,
		H264_ANNEXB,  // OME's default internal bitstream format for H264
		H264_RTP_RFC_6184,
		HVCC,		  // H.265 HVCC
		H265_ANNEXB,  // OME's default internal bitstream format for H265
		H265_RTP_RFC_7798,
		VP8,  // raw - OME's default internal bitstream format for VP8
		VP8_RTP_RFC_7741,
		AAC_RAW,
		AAC_MPEG4_GENERIC,
		AAC_ADTS,  // OME's default internal bitstream format for AAC
		AAC_LATM,
		OPUS,  // raw - OME's default internal bitstream format for OPUS
		OPUS_RTP_RFC_7587,
		MP3,
		JPEG,
		PNG,
		WEBP,

		// For Data Track
		ID3v2,
		OVEN_EVENT,	 // OvenMediaEngine defined event
		CUE,
		AMF,	 // AMF0
		SEI,	 // H.264/H.265 SEI
		SCTE35,	 // SCTE35
		WebVTT	 // WebVTT (Web Video Text Tracks)
	};

	enum class PacketType : int8_t
	{
		Unknown = -1,
		// This is a special purpose packet type, used by the ovt provider,
		// and the "media router" delivers this type of packet to the publisher as it is without parsing.
		OVT		= 0,

		RAW,  // AAC RAW, AAC ADTS, JPEG
		// H.264
		SEQUENCE_HEADER,  // For H.264 AVCC, AAC RAW
		NALU,			  // For H.264 AVCC, ANNEXB

		// For Data Track
		EVENT,
		VIDEO_EVENT,
		AUDIO_EVENT,
	};

	enum class PictureType : uint8_t
	{
		None = 0,  ///< Undefined
		I,		   ///< Intra
		P,		   ///< Predicted
		B,		   ///< Bi-dir predicted
		S,		   ///< S(GMC)-VOP MPEG4
		SI,		   ///< Switching Intra
		SP,		   ///< Switching Predicted
		BI,		   ///< BI type
	};

	enum class MediaCodecId : uint8_t
	{
		None = 0,
		H264,
		H265,
		Vp8,
		Vp9,
		Av1,
		Flv,
		Aac,
		Mp3,
		Opus,
		Jpeg,
		Png,
		Webp,
		WebVTT,
		Whisper
	};

	// DeviceId is used to identify a hwardware accelerator device.
	typedef int32_t DeviceId;

	enum class MediaCodecModuleId : uint8_t
	{
		None = 0,
		DEFAULT,   // SW
		OPENH264,  // SW
		X264,	   // SW
		NVENC,	   // HW
		QSV,	   // HW
		XMA,	   // HW
		NILOGAN,   // HW
		LIBVPX,	   // SW
		FDKAAC,	   // SW
		LIBOPUS,   // SW
		NB
	};

	constexpr const char *GetMediaCodecModuleIdString(MediaCodecModuleId module_id)
	{
		switch (module_id)
		{
			OV_CASE_RETURN_ENUM_STRING(MediaCodecModuleId, None);
			OV_CASE_RETURN_ENUM_STRING(MediaCodecModuleId, DEFAULT);
			OV_CASE_RETURN_ENUM_STRING(MediaCodecModuleId, OPENH264);
			OV_CASE_RETURN_ENUM_STRING(MediaCodecModuleId, X264);
			OV_CASE_RETURN_ENUM_STRING(MediaCodecModuleId, NVENC);
			OV_CASE_RETURN_ENUM_STRING(MediaCodecModuleId, QSV);
			OV_CASE_RETURN_ENUM_STRING(MediaCodecModuleId, XMA);
			OV_CASE_RETURN_ENUM_STRING(MediaCodecModuleId, NILOGAN);
			OV_CASE_RETURN_ENUM_STRING(MediaCodecModuleId, LIBVPX);
			OV_CASE_RETURN_ENUM_STRING(MediaCodecModuleId, FDKAAC);
			OV_CASE_RETURN_ENUM_STRING(MediaCodecModuleId, LIBOPUS);
			OV_CASE_RETURN_ENUM_STRING(MediaCodecModuleId, NB);
		}

		return "Unknown";
	}

	enum class VideoPixelFormatId : int32_t
	{
		None = 0,
		YUVJ444P,
		YUVJ422P,
		YUVJ420P,
		YUVA420P,
		YUV444P9,
		YUV444P16,
		YUV444P12,
		YUV444P10,
		YUV444P,
		YUV440P12,
		YUV440P10,
		YUV440P,
		YUV422P12,
		YUV422P10,
		YUV422P,
		YUV420P9,
		YUV420P12,
		YUV420P10,
		YUV420P,
		RGB24,
		P016,
		P010,
		NV21,
		NV20,
		NV16,
		NV12,
		GRAY8,
		GRAY10,
		GBRP16,
		GBRP12,
		GBRP10,
		GBRP,
		BGR24,
		BGR0,
		ARGB,
		RGBA,
		ABGR,
		BGRA,
		CUDA,
		XVBM_8,
		XVBM_10,
		NB
	};

	constexpr const char *GetVideoPixelFormatIdString(VideoPixelFormatId format)
	{
		switch (format)
		{
			OV_CASE_RETURN_ENUM_STRING(VideoPixelFormatId, None);
			OV_CASE_RETURN_ENUM_STRING(VideoPixelFormatId, YUVJ444P);
			OV_CASE_RETURN_ENUM_STRING(VideoPixelFormatId, YUVJ422P);
			OV_CASE_RETURN_ENUM_STRING(VideoPixelFormatId, YUVJ420P);
			OV_CASE_RETURN_ENUM_STRING(VideoPixelFormatId, YUVA420P);
			OV_CASE_RETURN_ENUM_STRING(VideoPixelFormatId, YUV444P9);
			OV_CASE_RETURN_ENUM_STRING(VideoPixelFormatId, YUV444P16);
			OV_CASE_RETURN_ENUM_STRING(VideoPixelFormatId, YUV444P12);
			OV_CASE_RETURN_ENUM_STRING(VideoPixelFormatId, YUV444P10);
			OV_CASE_RETURN_ENUM_STRING(VideoPixelFormatId, YUV444P);
			OV_CASE_RETURN_ENUM_STRING(VideoPixelFormatId, YUV440P12);
			OV_CASE_RETURN_ENUM_STRING(VideoPixelFormatId, YUV440P10);
			OV_CASE_RETURN_ENUM_STRING(VideoPixelFormatId, YUV440P);
			OV_CASE_RETURN_ENUM_STRING(VideoPixelFormatId, YUV422P12);
			OV_CASE_RETURN_ENUM_STRING(VideoPixelFormatId, YUV422P10);
			OV_CASE_RETURN_ENUM_STRING(VideoPixelFormatId, YUV422P);
			OV_CASE_RETURN_ENUM_STRING(VideoPixelFormatId, YUV420P9);
			OV_CASE_RETURN_ENUM_STRING(VideoPixelFormatId, YUV420P12);
			OV_CASE_RETURN_ENUM_STRING(VideoPixelFormatId, YUV420P10);
			OV_CASE_RETURN_ENUM_STRING(VideoPixelFormatId, YUV420P);
			OV_CASE_RETURN_ENUM_STRING(VideoPixelFormatId, RGB24);
			OV_CASE_RETURN_ENUM_STRING(VideoPixelFormatId, P016);
			OV_CASE_RETURN_ENUM_STRING(VideoPixelFormatId, P010);
			OV_CASE_RETURN_ENUM_STRING(VideoPixelFormatId, NV21);
			OV_CASE_RETURN_ENUM_STRING(VideoPixelFormatId, NV20);
			OV_CASE_RETURN_ENUM_STRING(VideoPixelFormatId, NV16);
			OV_CASE_RETURN_ENUM_STRING(VideoPixelFormatId, NV12);
			OV_CASE_RETURN_ENUM_STRING(VideoPixelFormatId, GRAY8);
			OV_CASE_RETURN_ENUM_STRING(VideoPixelFormatId, GRAY10);
			OV_CASE_RETURN_ENUM_STRING(VideoPixelFormatId, GBRP16);
			OV_CASE_RETURN_ENUM_STRING(VideoPixelFormatId, GBRP12);
			OV_CASE_RETURN_ENUM_STRING(VideoPixelFormatId, GBRP10);
			OV_CASE_RETURN_ENUM_STRING(VideoPixelFormatId, GBRP);
			OV_CASE_RETURN_ENUM_STRING(VideoPixelFormatId, BGR24);
			OV_CASE_RETURN_ENUM_STRING(VideoPixelFormatId, BGR0);
			OV_CASE_RETURN_ENUM_STRING(VideoPixelFormatId, ARGB);
			OV_CASE_RETURN_ENUM_STRING(VideoPixelFormatId, RGBA);
			OV_CASE_RETURN_ENUM_STRING(VideoPixelFormatId, ABGR);
			OV_CASE_RETURN_ENUM_STRING(VideoPixelFormatId, BGRA);
			OV_CASE_RETURN_ENUM_STRING(VideoPixelFormatId, CUDA);
			OV_CASE_RETURN_ENUM_STRING(VideoPixelFormatId, XVBM_8);
			OV_CASE_RETURN_ENUM_STRING(VideoPixelFormatId, XVBM_10);
			OV_CASE_RETURN_ENUM_STRING(VideoPixelFormatId, NB);
		}
		return "Unknown";
	}

	enum class KeyFrameIntervalType : uint8_t
	{
		FRAME = 0,
		TIME
	};

	constexpr static bool IsVideoCodec(cmn::MediaCodecId codec_id)
	{
		switch (codec_id)
		{
			OV_CASE_RETURN(cmn::MediaCodecId::None, false);

			OV_CASE_RETURN(cmn::MediaCodecId::H264, true);
			OV_CASE_RETURN(cmn::MediaCodecId::H265, true);
			OV_CASE_RETURN(cmn::MediaCodecId::Vp8, true);
			OV_CASE_RETURN(cmn::MediaCodecId::Vp9, true);
			OV_CASE_RETURN(cmn::MediaCodecId::Av1, true);
			OV_CASE_RETURN(cmn::MediaCodecId::Flv, true);

			OV_CASE_RETURN(cmn::MediaCodecId::Aac, false);
			OV_CASE_RETURN(cmn::MediaCodecId::Mp3, false);
			OV_CASE_RETURN(cmn::MediaCodecId::Opus, false);

			OV_CASE_RETURN(cmn::MediaCodecId::Jpeg, false);
			OV_CASE_RETURN(cmn::MediaCodecId::Png, false);
			OV_CASE_RETURN(cmn::MediaCodecId::Webp, false);
			OV_CASE_RETURN(cmn::MediaCodecId::WebVTT, false);
			OV_CASE_RETURN(cmn::MediaCodecId::Whisper, false);
		}

		return false;
	}

	constexpr static bool IsImageCodec(cmn::MediaCodecId codec_id)
	{
		switch (codec_id)
		{
			OV_CASE_RETURN(cmn::MediaCodecId::None, false);

			OV_CASE_RETURN(cmn::MediaCodecId::H264, true);
			OV_CASE_RETURN(cmn::MediaCodecId::H265, true);
			OV_CASE_RETURN(cmn::MediaCodecId::Vp8, true);
			OV_CASE_RETURN(cmn::MediaCodecId::Vp9, true);
			OV_CASE_RETURN(cmn::MediaCodecId::Av1, true);
			OV_CASE_RETURN(cmn::MediaCodecId::Flv, true);

			OV_CASE_RETURN(cmn::MediaCodecId::Aac, false);
			OV_CASE_RETURN(cmn::MediaCodecId::Mp3, false);
			OV_CASE_RETURN(cmn::MediaCodecId::Opus, false);

			OV_CASE_RETURN(cmn::MediaCodecId::Jpeg, true);
			OV_CASE_RETURN(cmn::MediaCodecId::Png, true);
			OV_CASE_RETURN(cmn::MediaCodecId::Webp, true);
			OV_CASE_RETURN(cmn::MediaCodecId::WebVTT, false);
			OV_CASE_RETURN(cmn::MediaCodecId::Whisper, false);
		}

		return false;
	}

	constexpr static bool IsAudioCodec(cmn::MediaCodecId codec_id)
	{
		switch (codec_id)
		{
			OV_CASE_RETURN(cmn::MediaCodecId::None, false);

			OV_CASE_RETURN(cmn::MediaCodecId::H264, false);
			OV_CASE_RETURN(cmn::MediaCodecId::H265, false);
			OV_CASE_RETURN(cmn::MediaCodecId::Vp8, false);
			OV_CASE_RETURN(cmn::MediaCodecId::Vp9, false);
			OV_CASE_RETURN(cmn::MediaCodecId::Av1, false);
			OV_CASE_RETURN(cmn::MediaCodecId::Flv, false);

			OV_CASE_RETURN(cmn::MediaCodecId::Aac, true);
			OV_CASE_RETURN(cmn::MediaCodecId::Mp3, true);
			OV_CASE_RETURN(cmn::MediaCodecId::Opus, true);

			OV_CASE_RETURN(cmn::MediaCodecId::Jpeg, false);
			OV_CASE_RETURN(cmn::MediaCodecId::Png, false);
			OV_CASE_RETURN(cmn::MediaCodecId::Webp, false);
			OV_CASE_RETURN(cmn::MediaCodecId::WebVTT, false);
			OV_CASE_RETURN(cmn::MediaCodecId::Whisper, false);
		}

		return false;
	}

	constexpr const char *GetMediaPacketTypeString(cmn::PacketType packet_type)
	{
		switch (packet_type)
		{
			OV_CASE_RETURN_ENUM_STRING(PacketType, Unknown);
			OV_CASE_RETURN_ENUM_STRING(PacketType, OVT);
			OV_CASE_RETURN_ENUM_STRING(PacketType, RAW);
			OV_CASE_RETURN_ENUM_STRING(PacketType, SEQUENCE_HEADER);
			OV_CASE_RETURN_ENUM_STRING(PacketType, NALU);
			// Dimiden - Kept this value as it was returning `Unknown`
			// before refactoring due to not being handled in the case statement
			OV_CASE_RETURN(PacketType::EVENT, "Unknown");
			OV_CASE_RETURN_ENUM_STRING(PacketType, VIDEO_EVENT);
			OV_CASE_RETURN_ENUM_STRING(PacketType, AUDIO_EVENT);
		}

		return "Unknown";
	}

	constexpr const char *GetMediaTypeString(cmn::MediaType media_type)
	{
		switch (media_type)
		{
			OV_CASE_RETURN_ENUM_STRING(MediaType, Unknown);
			OV_CASE_RETURN_ENUM_STRING(MediaType, Video);
			OV_CASE_RETURN_ENUM_STRING(MediaType, Audio);
			OV_CASE_RETURN_ENUM_STRING(MediaType, Data);
			OV_CASE_RETURN_ENUM_STRING(MediaType, Subtitle);
			OV_CASE_RETURN_ENUM_STRING(MediaType, Attachment);
			// Dimiden - Kept this value as it was returning `Unknown`
			// before refactoring due to not being handled in the case statement
			OV_CASE_RETURN(MediaType::Nb, "Unknown");
		}

		return "Unknown";
	}

	constexpr const char *GetBitstreamFormatString(cmn::BitstreamFormat format)
	{
		switch (format)
		{
			OV_CASE_RETURN_ENUM_STRING(BitstreamFormat, Unknown);
			OV_CASE_RETURN(BitstreamFormat::H264_AVCC, "AVCC");
			OV_CASE_RETURN_ENUM_STRING(BitstreamFormat, H264_ANNEXB);
			OV_CASE_RETURN_ENUM_STRING(BitstreamFormat, H264_RTP_RFC_6184);
			OV_CASE_RETURN_ENUM_STRING(BitstreamFormat, H265_ANNEXB);
			OV_CASE_RETURN_ENUM_STRING(BitstreamFormat, H265_RTP_RFC_7798);
			OV_CASE_RETURN_ENUM_STRING(BitstreamFormat, HVCC);
			OV_CASE_RETURN_ENUM_STRING(BitstreamFormat, VP8);
			OV_CASE_RETURN_ENUM_STRING(BitstreamFormat, VP8_RTP_RFC_7741);
			OV_CASE_RETURN_ENUM_STRING(BitstreamFormat, AAC_RAW);
			OV_CASE_RETURN_ENUM_STRING(BitstreamFormat, AAC_MPEG4_GENERIC);
			OV_CASE_RETURN_ENUM_STRING(BitstreamFormat, AAC_ADTS);
			OV_CASE_RETURN_ENUM_STRING(BitstreamFormat, AAC_LATM);
			OV_CASE_RETURN_ENUM_STRING(BitstreamFormat, MP3);
			OV_CASE_RETURN_ENUM_STRING(BitstreamFormat, OPUS);
			OV_CASE_RETURN_ENUM_STRING(BitstreamFormat, OPUS_RTP_RFC_7587);
			OV_CASE_RETURN_ENUM_STRING(BitstreamFormat, JPEG);
			OV_CASE_RETURN_ENUM_STRING(BitstreamFormat, PNG);
			OV_CASE_RETURN_ENUM_STRING(BitstreamFormat, WEBP);
			OV_CASE_RETURN_ENUM_STRING(BitstreamFormat, ID3v2);
			OV_CASE_RETURN_ENUM_STRING(BitstreamFormat, OVEN_EVENT);
			OV_CASE_RETURN_ENUM_STRING(BitstreamFormat, CUE);
			OV_CASE_RETURN_ENUM_STRING(BitstreamFormat, AMF);
			OV_CASE_RETURN_ENUM_STRING(BitstreamFormat, SEI);
			OV_CASE_RETURN_ENUM_STRING(BitstreamFormat, SCTE35);
			OV_CASE_RETURN_ENUM_STRING(BitstreamFormat, WebVTT);
		}

		return "Unknown";
	}

	static cmn::MediaCodecModuleId GetCodecModuleIdByName(ov::String name)
	{
		name.MakeUpper();

		if (name.HasSuffix("_OPENH264") || name.HasSuffix("OPENH264"))
		{
			return cmn::MediaCodecModuleId::OPENH264;
		}
		else if (name.HasSuffix("_NVENC") || name.HasSuffix("_NV") || name.HasSuffix("NV") || name.HasSuffix("NVENC"))
		{
			return cmn::MediaCodecModuleId::NVENC;
		}
		else if (name.HasSuffix("_QSV") || name.HasSuffix("QSV"))
		{
			return cmn::MediaCodecModuleId::QSV;
		}
		else if (name.HasSuffix("_NILOGAN") || name.HasSuffix("NILOGAN"))
		{
			return cmn::MediaCodecModuleId::NILOGAN;
		}
		else if (name.HasSuffix("_XMA") || name.HasSuffix("XMA"))
		{
			return cmn::MediaCodecModuleId::XMA;
		}
		else if (name.HasSuffix("_LIBVPX") || name.HasSuffix("LIBVPX"))
		{
			return cmn::MediaCodecModuleId::LIBVPX;
		}
		else if (name.HasSuffix("_FDKAAC") || name.HasSuffix("FDKAAC"))
		{
			return cmn::MediaCodecModuleId::FDKAAC;
		}
		else if (name.HasSuffix("_X264") || name.HasSuffix("X264"))
		{
			return cmn::MediaCodecModuleId::X264;
		}
		else if (name.HasSuffix("_DEFAULT") || name.HasSuffix("DEFAULT"))
		{
			return cmn::MediaCodecModuleId::DEFAULT;
		}

		return cmn::MediaCodecModuleId::None;
	}

	constexpr const char *GetCodecModuleIdString(MediaCodecModuleId id)
	{
		switch (id)
		{
			OV_CASE_RETURN(MediaCodecModuleId::DEFAULT, "default");
			OV_CASE_RETURN(MediaCodecModuleId::OPENH264, "openh264");
			OV_CASE_RETURN(MediaCodecModuleId::NVENC, "nv");
			OV_CASE_RETURN(MediaCodecModuleId::QSV, "qsv");
			OV_CASE_RETURN(MediaCodecModuleId::NILOGAN, "nilogan");
			OV_CASE_RETURN(MediaCodecModuleId::XMA, "xma";);
			OV_CASE_RETURN(MediaCodecModuleId::LIBVPX, "libvpx");
			OV_CASE_RETURN(MediaCodecModuleId::FDKAAC, "fdkaac");
			OV_CASE_RETURN(MediaCodecModuleId::LIBOPUS, "libopus");
			OV_CASE_RETURN(MediaCodecModuleId::X264, "x264");
			OV_CASE_RETURN(MediaCodecModuleId::None, "none");
			// Dimiden - Kept this value as it was returning `none`
			// before refactoring due to not being handled in the case statement
			OV_CASE_RETURN(MediaCodecModuleId::NB, "none");
		}

		return "none";
	}

	constexpr static bool IsSupportHWAccels(cmn::MediaCodecModuleId id)
	{
		switch (id)
		{
			case cmn::MediaCodecModuleId::NVENC:
			case cmn::MediaCodecModuleId::QSV:
			case cmn::MediaCodecModuleId::XMA:
			case cmn::MediaCodecModuleId::NILOGAN:
				return true;
			default:
				break;
		}

		return false;
	}

	constexpr const char *GetCodecIdString(cmn::MediaCodecId id)
	{
		switch (id)
		{
			OV_CASE_RETURN_ENUM_STRING(MediaCodecId, None);
			// Video codecs
			OV_CASE_RETURN_ENUM_STRING(MediaCodecId, H264);
			OV_CASE_RETURN_ENUM_STRING(MediaCodecId, H265);
			OV_CASE_RETURN(MediaCodecId::Vp8, "VP8");
			OV_CASE_RETURN(MediaCodecId::Vp9, "VP9");
			OV_CASE_RETURN(MediaCodecId::Flv, "FLV");
			OV_CASE_RETURN(MediaCodecId::Av1, "AV1");
			// Image codecs
			OV_CASE_RETURN(MediaCodecId::Jpeg, "JPEG");
			OV_CASE_RETURN(MediaCodecId::Png, "PNG");
			OV_CASE_RETURN(MediaCodecId::Webp, "WEBP");
			// Audio codecs
			OV_CASE_RETURN(MediaCodecId::Aac, "AAC");
			OV_CASE_RETURN(MediaCodecId::Mp3, "MP3");
			OV_CASE_RETURN(MediaCodecId::Opus, "OPUS");
			// Subtitle codecs
			OV_CASE_RETURN(MediaCodecId::WebVTT, "WebVTT");
			OV_CASE_RETURN(MediaCodecId::Whisper, "WHISPER");
		}

		return "Unknown";
	}

	static cmn::MediaCodecId GetCodecIdByName(ov::String name)
	{
		name.MakeUpper();

		// Video codecs
		if (name.HasPrefix("H264"))
		{
			return cmn::MediaCodecId::H264;
		}
		else if (name.HasPrefix("H265"))
		{
			return cmn::MediaCodecId::H265;
		}
		else if (name.HasPrefix("VP8"))
		{
			return cmn::MediaCodecId::Vp8;
		}
		else if (name.HasPrefix("VP9"))
		{
			return cmn::MediaCodecId::Vp9;
		}
		else if (name.HasPrefix("FLV"))
		{
			return cmn::MediaCodecId::Flv;
		}
		// Image codecs
		else if (name.HasPrefix("JPEG"))
		{
			return cmn::MediaCodecId::Jpeg;
		}
		else if (name.HasPrefix("PNG"))
		{
			return cmn::MediaCodecId::Png;
		}
		else if (name.HasPrefix("WEBP"))
		{
			return cmn::MediaCodecId::Webp;
		}
		// Audio codecs
		else if (name.HasPrefix("AAC"))
		{
			return cmn::MediaCodecId::Aac;
		}
		else if (name.HasPrefix("MP3"))
		{
			return cmn::MediaCodecId::Mp3;
		}
		else if (name.HasPrefix("OPUS"))
		{
			return cmn::MediaCodecId::Opus;
		}

		return cmn::MediaCodecId::None;
	}

	static constexpr const char *GetKeyFrameIntervalTypeToString(cmn::KeyFrameIntervalType type)
	{
		switch (type)
		{
			case cmn::KeyFrameIntervalType::FRAME:
				return "frame";
			case cmn::KeyFrameIntervalType::TIME:
				return "time";
			default:
				return "unknown";
		}
	}

	static cmn::KeyFrameIntervalType GetKeyFrameIntervalTypeByName(ov::String type)
	{
		type.MakeLower();

		if (type == "frame")
		{
			return cmn::KeyFrameIntervalType::FRAME;
		}
		else if (type == "time")
		{
			return cmn::KeyFrameIntervalType::TIME;
		}

		return cmn::KeyFrameIntervalType::FRAME;
	}

	//////////////////////////////////////////////////////////////////////////////////////////////////
	// Timebase
	//////////////////////////////////////////////////////////////////////////////////////////////////

	class Timebase
	{
	public:
		Timebase()
			: Timebase(0, 0)
		{
		}

		Timebase(int32_t num, int32_t den)
			: _num(num), _den(den)
		{
		}

		Timebase &operator=(const Timebase &T) noexcept
		{
			_num = T._num;
			_den = T._den;

			return *this;
		}

		void Set(int32_t num, int32_t den)
		{
			SetNum(num);
			SetDen(den);
		}

		void SetNum(int32_t num)
		{
			_num = num;
		}

		void SetDen(int32_t den)
		{
			_den = den;
		}

		int32_t GetNum() const
		{
			return _num;
		}

		int32_t GetDen() const
		{
			return _den;
		}

		double GetExpr() const
		{
			if (_den == 0)
			{
				return 0.0;
			}

			return (double)_num / (double)_den;
		}

		double GetTimescale() const
		{
			if (_num == 0)
			{
				return 0.0;
			}

			return (double)_den / (double)_num;
		}

		bool IsValid() const
		{
			return (_num > 0) && (_den > 0);
		}

		bool operator==(const Timebase &timebase) const
		{
			return (timebase._num == _num) && (timebase._den == _den);
		}

		bool operator!=(const Timebase &timebase) const
		{
			return operator==(timebase) == false;
		}

		ov::String GetStringExpr() const
		{
			return ov::String::FormatString("%d/%d", GetNum(), GetDen());
		}

		ov::String ToString() const
		{
			return GetStringExpr();
		}

	private:
		// 1/1000
		int32_t _num;
		int32_t _den;
	};

	//////////////////////////////////////////////////////////////////////////////////////////////////
	// Audio sample format
	//////////////////////////////////////////////////////////////////////////////////////////////////

	// Defines values for compatibility with FFmpeg SampleFormat
	class AudioSample
	{
	public:
		// 샘플 포맷
		enum class Format : int8_t
		{
			None = -1,

			U8	 = 0,  ///< unsigned 8 bits
			S16	 = 1,  ///< signed 16 bits
			S32	 = 2,  ///< signed 32 bits
			Flt	 = 3,  ///< float
			Dbl	 = 4,  ///< double

			U8P	 = 5,  ///< unsigned 8 bits, planar
			S16P = 6,  ///< signed 16 bits, planar
			S32P = 7,  ///< signed 32 bits, planar
			FltP = 8,  ///< float, planar
			DblP = 9,  ///< double, planar

			Nb
		};

		// Samplerates
		enum class Rate : int32_t
		{
			None	 = 0,
			R8000	 = 8000,
			R11025	 = 11025,
			R16000	 = 16000,
			R22050	 = 22050,
			R32000	 = 32000,
			R44056	 = 44056,
			R44100	 = 44100,
			R47250	 = 47250,
			R48000	 = 48000,
			R50000	 = 50000,
			R50400	 = 50400,
			R88200	 = 88200,
			R96000	 = 96000,
			R176400	 = 176400,
			R192000	 = 192000,
			R352800	 = 352800,
			R2822400 = 2822400,
			R5644800 = 5644800,

			Nb
		};

	public:
		AudioSample() = default;

		explicit AudioSample(Format fmt)
		{
			SetFormat(fmt);
		}

		~AudioSample() = default;

		AudioSample &operator=(const AudioSample &T) noexcept
		{
			_sample_size = T._sample_size;
			_format		 = T._format;
			_name		 = T._name;
			_rate		 = T._rate;

			return *this;
		}

#define OV_MEDIA_TYPE_SET_VALUE(type, ...) \
	case type:                             \
		__VA_ARGS__;                       \
		break

		void SetFormat(Format fmt)
		{
			_format = fmt;

			switch (_format)
			{
				OV_MEDIA_TYPE_SET_VALUE(Format::U8, _name = "u8", _sample_size = 1);
				OV_MEDIA_TYPE_SET_VALUE(Format::S16, _name = "s16", _sample_size = 2);
				OV_MEDIA_TYPE_SET_VALUE(Format::S32, _name = "s32", _sample_size = 4);
				OV_MEDIA_TYPE_SET_VALUE(Format::Flt, _name = "flt", _sample_size = 4);
				OV_MEDIA_TYPE_SET_VALUE(Format::Dbl, _name = "dbl", _sample_size = 8);
				OV_MEDIA_TYPE_SET_VALUE(Format::U8P, _name = "u8p", _sample_size = 1);
				OV_MEDIA_TYPE_SET_VALUE(Format::S16P, _name = "s16p", _sample_size = 2);
				OV_MEDIA_TYPE_SET_VALUE(Format::S32P, _name = "s32p", _sample_size = 4);
				OV_MEDIA_TYPE_SET_VALUE(Format::FltP, _name = "fltp", _sample_size = 4);
				OV_MEDIA_TYPE_SET_VALUE(Format::DblP, _name = "dblp", _sample_size = 8);

				default:
					OV_MEDIA_TYPE_SET_VALUE(Format::None, _name = "none", _sample_size = 0);
			}
		}

		// Unit: Bytes
		int32_t GetSampleSize() const
		{
			return _sample_size;
		}

		AudioSample::Format GetFormat() const
		{
			return _format;
		}

		void SetRate(Rate rate)
		{
			_rate = rate;
		}

		Rate GetRate() const
		{
			return _rate;
		}

		int32_t GetRateNum() const
		{
			return ov::ToUnderlyingType(_rate);
		}

		const char *GetName() const
		{
			return _name.c_str();
		}

		// Sample rate
		Rate _rate			 = Rate::None;

		// Sample format
		Format _format		 = Format::None;

		// Sample size
		int32_t _sample_size = 0;

		// Sample format name
		std::string _name	 = "none";
	};

	template <typename... Args>
	static constexpr uint32_t MakeAudioChannelLayout(Args... channels)
	{
		return (0u | ... | static_cast<uint32_t>(channels));
	}

	//////////////////////////////////////////////////////////////////////////////////////////////////
	// Audio layout
	//////////////////////////////////////////////////////////////////////////////////////////////////
	class AudioChannel
	{
	public:
		// Defines values for compatibility with FFmpeg ChannelName
		enum class Channel : uint32_t
		{
			None			   = 0x00000000U,
			FrontLeft		   = 0x00000001U,
			FrontRight		   = 0x00000002U,
			FrontCenter		   = 0x00000004U,
			LowFrequency	   = 0x00000008U,
			BackLeft		   = 0x00000010U,
			BackRight		   = 0x00000020U,
			FrontLeftOfCenter  = 0x00000040U,
			FrontRightOfCenter = 0x00000080U,
			BackCenter		   = 0x00000100U,
			SideLeft		   = 0x00000200U,
			SideRight		   = 0x00000400U,
			TopCenter		   = 0x00000800U,
			TopFrontLeft	   = 0x00001000U,
			TopFrontCenter	   = 0x00002000U,
			TopFrontRight	   = 0x00004000U,
			TopBackLeft		   = 0x00008000U,
			TopBackCenter	   = 0x00010000U,
			TopBackRight	   = 0x00020000U,
			StereoLeft		   = 0x20000000U,
			StereoRight		   = 0x40000000U
		};

		// Defines values for compatibility with FFmpeg ChannelLayout
		enum class Layout : uint32_t
		{
			LayoutUnknown		  = MakeAudioChannelLayout(Channel::None),
			LayoutMono			  = MakeAudioChannelLayout(Channel::FrontCenter),
			LayoutStereo		  = MakeAudioChannelLayout(Channel::FrontLeft, Channel::FrontRight),
			Layout2Point1		  = MakeAudioChannelLayout(Channel::FrontLeft, Channel::FrontRight, Channel::LowFrequency),
			Layout21			  = MakeAudioChannelLayout(Channel::FrontLeft, Channel::FrontRight, Channel::BackCenter),
			LayoutSurround		  = MakeAudioChannelLayout(Channel::FrontLeft, Channel::FrontRight, Channel::FrontCenter),
			Layout3Point1		  = MakeAudioChannelLayout(Channel::FrontLeft, Channel::FrontRight, Channel::FrontCenter, Channel::LowFrequency),
			Layout4Point0		  = MakeAudioChannelLayout(Channel::FrontLeft, Channel::FrontRight, Channel::FrontCenter, Channel::BackCenter),
			Layout4Point1		  = MakeAudioChannelLayout(Channel::FrontLeft, Channel::FrontRight, Channel::FrontCenter, Channel::BackCenter, Channel::LowFrequency),
			Layout22			  = MakeAudioChannelLayout(Channel::FrontLeft, Channel::FrontRight, Channel::SideLeft, Channel::SideRight),
			LayoutQuad			  = MakeAudioChannelLayout(Channel::FrontLeft, Channel::FrontRight, Channel::BackLeft, Channel::BackRight),
			Layout5Point0		  = MakeAudioChannelLayout(Channel::FrontLeft, Channel::FrontRight, Channel::FrontCenter, Channel::SideLeft, Channel::SideRight),
			Layout5Point1		  = MakeAudioChannelLayout(Channel::FrontLeft, Channel::FrontRight, Channel::FrontCenter, Channel::SideLeft, Channel::SideRight, Channel::LowFrequency),
			Layout5Point0Back	  = MakeAudioChannelLayout(Channel::FrontLeft, Channel::FrontRight, Channel::FrontCenter, Channel::BackLeft, Channel::BackRight),
			Layout5Point1Back	  = MakeAudioChannelLayout(Channel::FrontLeft, Channel::FrontRight, Channel::FrontCenter, Channel::BackLeft, Channel::BackRight, Channel::LowFrequency),
			Layout6Point0		  = MakeAudioChannelLayout(Channel::FrontLeft, Channel::FrontRight, Channel::FrontCenter, Channel::SideLeft, Channel::SideRight, Channel::BackCenter),
			Layout6Point0Front	  = MakeAudioChannelLayout(Channel::FrontLeft, Channel::FrontRight, Channel::SideLeft, Channel::SideRight, Channel::FrontLeftOfCenter, Channel::FrontRightOfCenter),
			LayoutHexagonal		  = MakeAudioChannelLayout(Channel::FrontLeft, Channel::FrontRight, Channel::FrontCenter, Channel::BackLeft, Channel::BackRight, Channel::BackCenter),
			Layout6Point1		  = MakeAudioChannelLayout(Channel::FrontLeft, Channel::FrontRight, Channel::FrontCenter, Channel::SideLeft, Channel::SideRight, Channel::LowFrequency, Channel::BackCenter),
			Layout6Point1Back	  = MakeAudioChannelLayout(Channel::FrontLeft, Channel::FrontRight, Channel::FrontCenter, Channel::BackLeft, Channel::BackRight, Channel::LowFrequency, Channel::BackCenter),
			Layout6Point1Front	  = MakeAudioChannelLayout(Channel::FrontLeft, Channel::FrontRight, Channel::SideLeft, Channel::SideRight, Channel::FrontLeftOfCenter, Channel::FrontRightOfCenter, Channel::LowFrequency),
			Layout7Point0		  = MakeAudioChannelLayout(Channel::FrontLeft, Channel::FrontRight, Channel::FrontCenter, Channel::SideLeft, Channel::SideRight, Channel::BackLeft, Channel::BackRight),
			Layout7Point0Front	  = MakeAudioChannelLayout(Channel::FrontLeft, Channel::FrontRight, Channel::FrontCenter, Channel::SideLeft, Channel::SideRight, Channel::FrontLeftOfCenter, Channel::FrontRightOfCenter),
			Layout7Point1		  = MakeAudioChannelLayout(Channel::FrontLeft, Channel::FrontRight, Channel::FrontCenter, Channel::SideLeft, Channel::SideRight, Channel::LowFrequency, Channel::BackLeft, Channel::BackRight),
			Layout7Point1Wide	  = MakeAudioChannelLayout(Channel::FrontLeft, Channel::FrontRight, Channel::FrontCenter, Channel::SideLeft, Channel::SideRight, Channel::LowFrequency, Channel::FrontLeftOfCenter, Channel::FrontRightOfCenter),
			Layout7Point1WideBack = MakeAudioChannelLayout(Channel::FrontLeft, Channel::FrontRight, Channel::FrontCenter, Channel::BackLeft, Channel::BackRight, Channel::LowFrequency, Channel::FrontLeftOfCenter, Channel::FrontRightOfCenter),
			LayoutOctagonal		  = MakeAudioChannelLayout(Channel::FrontLeft, Channel::FrontRight, Channel::FrontCenter, Channel::SideLeft, Channel::SideRight, Channel::BackLeft, Channel::BackRight, Channel::BackCenter),
		};

		constexpr static const char *GetLayoutName(Layout layout)
		{
			switch (layout)
			{
				OV_CASE_RETURN(Layout::LayoutUnknown, "unknown");
				OV_CASE_RETURN(Layout::LayoutMono, "mono");
				OV_CASE_RETURN(Layout::LayoutStereo, "stereo");
				OV_CASE_RETURN(Layout::Layout2Point1, "2.1");
				OV_CASE_RETURN(Layout::Layout21, "3.0(back)");
				OV_CASE_RETURN(Layout::LayoutSurround, "3.0");
				OV_CASE_RETURN(Layout::Layout3Point1, "3.1");
				OV_CASE_RETURN(Layout::Layout4Point0, "4.0");
				OV_CASE_RETURN(Layout::Layout4Point1, "4.1");
				OV_CASE_RETURN(Layout::Layout22, "quad(side)");
				OV_CASE_RETURN(Layout::LayoutQuad, "quad");
				OV_CASE_RETURN(Layout::Layout5Point0, "5.0(side)");
				OV_CASE_RETURN(Layout::Layout5Point1, "5.1(side)");
				OV_CASE_RETURN(Layout::Layout5Point0Back, "5.0");
				OV_CASE_RETURN(Layout::Layout5Point1Back, "5.1");
				OV_CASE_RETURN(Layout::Layout6Point0, "6.0");
				OV_CASE_RETURN(Layout::Layout6Point0Front, "6.0(front)");
				OV_CASE_RETURN(Layout::LayoutHexagonal, "hexagonal");
				OV_CASE_RETURN(Layout::Layout6Point1, "6.1");
				OV_CASE_RETURN(Layout::Layout6Point1Back, "6.1(back)");
				OV_CASE_RETURN(Layout::Layout6Point1Front, "6.1(front)");
				OV_CASE_RETURN(Layout::Layout7Point0, "7.0");
				OV_CASE_RETURN(Layout::Layout7Point0Front, "7.0(front)");
				OV_CASE_RETURN(Layout::Layout7Point1, "7.1");
				OV_CASE_RETURN(Layout::Layout7Point1Wide, "7.1(wide-side)");
				OV_CASE_RETURN(Layout::Layout7Point1WideBack, "7.1(wide)");
				OV_CASE_RETURN(Layout::LayoutOctagonal, "octagonal");
			}

			return "unknown";
		}

	public:
		AudioChannel()	= default;
		AudioChannel(AudioChannel::Layout layout)
		{
			SetLayout(layout);
		}
		
		~AudioChannel() = default;

		AudioChannel &operator=(const AudioChannel &audio_channel) noexcept
		{
			_layout = audio_channel._layout;
			_count	= audio_channel._count;
			_name	= audio_channel._name;

			return *this;
		}

		void SetLayout(Layout layout)
		{
			_layout = layout;

			switch (_layout)
			{
				OV_MEDIA_TYPE_SET_VALUE(Layout::LayoutUnknown, _count = 0, _name = GetLayoutName(Layout::LayoutUnknown));
				OV_MEDIA_TYPE_SET_VALUE(Layout::LayoutMono, _count = 1, _name = GetLayoutName(Layout::LayoutMono));
				OV_MEDIA_TYPE_SET_VALUE(Layout::LayoutStereo, _count = 2, _name = GetLayoutName(Layout::LayoutStereo));
				OV_MEDIA_TYPE_SET_VALUE(Layout::Layout2Point1, _count = 3, _name = GetLayoutName(Layout::Layout2Point1));
				OV_MEDIA_TYPE_SET_VALUE(Layout::Layout21, _count = 3, _name = GetLayoutName(Layout::Layout21));
				OV_MEDIA_TYPE_SET_VALUE(Layout::LayoutSurround, _count = 3, _name = GetLayoutName(Layout::LayoutSurround));
				OV_MEDIA_TYPE_SET_VALUE(Layout::Layout3Point1, _count = 4, _name = GetLayoutName(Layout::Layout3Point1));
				OV_MEDIA_TYPE_SET_VALUE(Layout::Layout4Point0, _count = 4, _name = GetLayoutName(Layout::Layout4Point0));
				OV_MEDIA_TYPE_SET_VALUE(Layout::Layout4Point1, _count = 5, _name = GetLayoutName(Layout::Layout4Point1));
				OV_MEDIA_TYPE_SET_VALUE(Layout::Layout22, _count = 4, _name = GetLayoutName(Layout::Layout22));
				OV_MEDIA_TYPE_SET_VALUE(Layout::LayoutQuad, _count = 4, _name = GetLayoutName(Layout::LayoutQuad));
				OV_MEDIA_TYPE_SET_VALUE(Layout::Layout5Point0, _count = 5, _name = GetLayoutName(Layout::Layout5Point0));
				OV_MEDIA_TYPE_SET_VALUE(Layout::Layout5Point1, _count = 6, _name = GetLayoutName(Layout::Layout5Point1));
				OV_MEDIA_TYPE_SET_VALUE(Layout::Layout5Point0Back, _count = 5, _name = GetLayoutName(Layout::Layout5Point0Back));
				OV_MEDIA_TYPE_SET_VALUE(Layout::Layout5Point1Back, _count = 6, _name = GetLayoutName(Layout::Layout5Point1Back));
				OV_MEDIA_TYPE_SET_VALUE(Layout::Layout6Point0, _count = 6, _name = GetLayoutName(Layout::Layout6Point0));
				OV_MEDIA_TYPE_SET_VALUE(Layout::Layout6Point0Front, _count = 6, _name = GetLayoutName(Layout::Layout6Point0Front));
				OV_MEDIA_TYPE_SET_VALUE(Layout::LayoutHexagonal, _count = 6, _name = GetLayoutName(Layout::LayoutHexagonal));
				OV_MEDIA_TYPE_SET_VALUE(Layout::Layout6Point1, _count = 7, _name = GetLayoutName(Layout::Layout6Point1));
				OV_MEDIA_TYPE_SET_VALUE(Layout::Layout6Point1Back, _count = 7, _name = GetLayoutName(Layout::Layout6Point1Back));
				OV_MEDIA_TYPE_SET_VALUE(Layout::Layout6Point1Front, _count = 7, _name = GetLayoutName(Layout::Layout6Point1Front));
				OV_MEDIA_TYPE_SET_VALUE(Layout::Layout7Point0, _count = 7, _name = GetLayoutName(Layout::Layout7Point0));
				OV_MEDIA_TYPE_SET_VALUE(Layout::Layout7Point0Front, _count = 7, _name = GetLayoutName(Layout::Layout7Point0Front));
				OV_MEDIA_TYPE_SET_VALUE(Layout::Layout7Point1, _count = 8, _name = GetLayoutName(Layout::Layout7Point1));
				OV_MEDIA_TYPE_SET_VALUE(Layout::Layout7Point1Wide, _count = 8, _name = GetLayoutName(Layout::Layout7Point1Wide));
				OV_MEDIA_TYPE_SET_VALUE(Layout::Layout7Point1WideBack, _count = 8, _name = GetLayoutName(Layout::Layout7Point1WideBack));
				OV_MEDIA_TYPE_SET_VALUE(Layout::LayoutOctagonal, _count = 8, _name = GetLayoutName(Layout::LayoutOctagonal));
			}
		}

		void SetLayout(std::optional<Layout> layout)
		{
			if (layout.has_value())
			{
				SetLayout(layout.value());
			}
		}

		// If it is set as channel count, it is used as the default layout.
		void SetCount(uint32_t count)
		{
			_count = count;
			switch (_count)
			{
				OV_MEDIA_TYPE_SET_VALUE(0, _layout = Layout::LayoutUnknown, _name = "unknown");
				OV_MEDIA_TYPE_SET_VALUE(1, _layout = Layout::LayoutMono, _name = "mono");
				OV_MEDIA_TYPE_SET_VALUE(2, _layout = Layout::LayoutStereo, _name = "stereo");
				OV_MEDIA_TYPE_SET_VALUE(3, _layout = Layout::Layout2Point1, _name = "2.1");
				OV_MEDIA_TYPE_SET_VALUE(4, _layout = Layout::Layout4Point0, _name = "4.0");
				OV_MEDIA_TYPE_SET_VALUE(5, _layout = Layout::Layout5Point0Back, _name = "5.0");
				OV_MEDIA_TYPE_SET_VALUE(6, _layout = Layout::Layout5Point1Back, _name = "5.1");
				OV_MEDIA_TYPE_SET_VALUE(7, _layout = Layout::Layout6Point1, _name = "6.1");
				OV_MEDIA_TYPE_SET_VALUE(8, _layout = Layout::Layout7Point1, _name = "7.1");
			}
		}

		// channel layout
		AudioChannel::Layout GetLayout() const
		{
			return _layout;
		}

		// channel count
		uint32_t GetCounts() const
		{
			return _count;
		}

		// the name of channel layout
		const char *GetName() const
		{
			return _name.c_str();
		}

		bool IsValid() const
		{
			return _layout != Layout::LayoutUnknown && _count > 0;
		}

	private:
		Layout _layout	  = Layout::LayoutStereo;
		uint32_t _count	  = 2;
		std::string _name = "stereo";
	};

	struct Resolution
	{
		int32_t width  = 0;
		int32_t height = 0;

		bool operator==(const Resolution &other) const
		{
			return width == other.width && height == other.height;
		}

		ov::String ToString()
		{
			return ov::String::FormatString("%dx%d", width, height);
		}
	};
}  // namespace cmn
