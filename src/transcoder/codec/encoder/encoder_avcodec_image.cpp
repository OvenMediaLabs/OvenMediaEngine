//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Keukhan
//  Copyright (c) 2026 OvenMediaLabs. All rights reserved.
//
//==============================================================================
#include "encoder_avcodec_image.h"

#include <modules/containers/avif/avif_packager.h>

#include "../../transcoder_private.h"

void AVCodecImageEncoder::SetParamsCommon()
{
	_codec.SetMediaType(cmn::MediaType::Video);
	_codec.SetFrameRate(cmn::Rational::FromDouble(GetRefTrack()->GetFrameRate()));
	_codec.SetTimeBase(GetRefTrack()->GetTimeBase());
	_codec.SetPixelFormat(GetSupportVideoFormat());
	auto resolution = GetRefTrack()->GetResolution();
	_codec.SetWidth(resolution.width);
	_codec.SetHeight(resolution.height);

	_bitstream_format = GetBitstreamFormat();
	_packet_type = cmn::PacketType::RAW;
}

bool AVCodecImageEncoder::SetParamsJpeg()
{
	SetParamsCommon();

	_codec.SetFixedQScale();
	_codec.SetGlobalQualityFromQp(_codec.GetQMin());

	// Set color range to JPEG
	_codec.SetColorRange(cmn::ColorRange::Full);
	_codec.SetStrictCompliance();

	return true;
}

bool AVCodecImageEncoder::SetParamsPng()
{
	SetParamsCommon();

	_codec.SetCompressionLevel(1);

	return true;
}

bool AVCodecImageEncoder::SetParamsWebp()
{
	SetParamsCommon();

	_codec.SetCompressionLevel(1);

	auto preset = GetRefTrack()->GetPreset();
	if (preset.IsEmpty())
	{
		_codec.SetOption("preset", "default");
	}
	else if (preset == "none" || preset == "default" || preset == "picture" ||
			 preset == "photo" || preset == "drawing" || preset == "icon" || preset == "text")
	{
		_codec.SetOption("preset", preset.CStr());
	}

	return true;
}

bool AVCodecImageEncoder::SetParamsAvif()
{
	SetParamsCommon();

	_codec.SetOption("usage", "allintra");
	_codec.SetGopSize(0);

	// The context defaults to UNSPECIFIED and libaom takes the CICP verbatim, so without these
	// the still carries no colour tag. It goes into the sequence header, hence before open.
	//
	// TODO(Keukhan): a non-BT.709 source (BT.601 SD, BT.2020/HLG) is mistagged - nothing in
	// the filter graph converts the frames.
	AVCodecContext *context	 = _codec.Get();
	context->color_primaries = AVCOL_PRI_BT709;
	context->color_trc		 = AVCOL_TRC_BT709;
	context->colorspace		 = AVCOL_SPC_BT709;
	_codec.SetColorRange(cmn::ColorRange::Limited);

	_codec.SetThreadCount(1);

	// The libaom encoder is very slow, so use the fastest preset FFmpeg allows and a reasonable
	// CRF. bit_rate 0 selects pure constant quality (AOM_Q) over AOM_CQ.
	_codec.SetOption("cpu-used", static_cast<int64_t>(8));
	_codec.SetOption("crf", static_cast<int64_t>(23));
	_codec.SetBitrate(0);

	return true;
}

bool AVCodecImageEncoder::OpenCodec()
{
	if (_codec.AllocEncoder(GetCodecID()) == false)
	{
		logte("Could not allocate encoder context for %s", cmn::GetCodecIdString(GetCodecID()));
		return false;
	}

	bool result = false;
	switch (_codec_id)
	{
		case cmn::MediaCodecId::Jpeg:
			result = SetParamsJpeg();
			break;
		case cmn::MediaCodecId::Png:
			result = SetParamsPng();
			break;
		case cmn::MediaCodecId::Avif:
			result = SetParamsAvif();
			break;
		case cmn::MediaCodecId::Webp:
		default:
			result = SetParamsWebp();
			break;
	}

	if (result == false)
	{
		logte("Could not set codec parameters for %s", cmn::GetCodecIdString(GetCodecID()));
		return false;
	}

	if (_codec.Open() == false)
	{
		logte("Could not open encoder(%s). %s", cmn::GetCodecIdString(GetCodecID()), _codec.GetLastErrorString().CStr());
		return false;
	}

	return true;
}

bool AVCodecImageEncoder::Initialize()
{
	auto result = OpenCodec();
	if (_track != nullptr)
	{
		_track->SetCodecStatus(result ? cmn::CodecStatus::Ready : cmn::CodecStatus::Failed);
	}

	return result;
}

void AVCodecImageEncoder::Uninitialize()
{
	_codec.Flush();
	_codec.Reset();
}

EncodeResult AVCodecImageEncoder::SendFrame(const std::shared_ptr<const MediaFrame> &frame, bool force_keyframe)
{
	// Flush the encoder if the frame is nullptr.
	if (frame == nullptr)
	{
		if (_codec.SendFrame(nullptr) != ffmpeg::CodecResult::Ok)
		{
			logte("Error sending a frame for encoding. reason(%s)", _codec.GetLastErrorString().CStr());
		}
		return EncodeResult::NoOutput();
	}

	auto result = _codec.SendFrame(frame, force_keyframe);
	if (result == ffmpeg::CodecResult::Again)
	{
		logtw("Encoder internal buffer is full, need to flush packets.");
	}
	else if (result == ffmpeg::CodecResult::InvalidData)
	{
		logtw("Invalid data while sending a frame for encoding.");
	}
	else if (result == ffmpeg::CodecResult::NoMemory)
	{
		logte("Could not allocate memory while sending a frame for encoding.");
		return EncodeResult::Error();
	}
	else if (result != ffmpeg::CodecResult::Ok)
	{
		logte("Error sending a frame for encoding. reason(%s)", _codec.GetLastErrorString().CStr());
		return EncodeResult::Error();
	}

	return EncodeResult::NoOutput();
}

EncodeResult AVCodecImageEncoder::ReceivePacket()
{
	auto [result, media_packet] = _codec.ReceivePacket(_bitstream_format, _packet_type);
	if (result == ffmpeg::CodecResult::Again || result == ffmpeg::CodecResult::Eof)
	{
		return EncodeResult::NoOutput();
	}
	else if (result == ffmpeg::CodecResult::InvalidData)
	{
		logtw("Invalid data while receiving a packet for encoding.");
		return EncodeResult::NoOutput();
	}
	else if (result == ffmpeg::CodecResult::NoMemory)
	{
		logtw("Could not allocate memory while receiving a packet for encoding.");
		return EncodeResult::Error();
	}
	else if (result != ffmpeg::CodecResult::Ok)
	{
		logte("Error receiving a packet for encoding : %s", _codec.GetLastErrorString().CStr());
		return EncodeResult::Error();
	}

	if (media_packet == nullptr)
	{
		logte("Could not allocate the media packet");
		return EncodeResult::Error();
	}

	// libaom emits the AV1 bitstream only, so wrap the still into its container here - an
	// image encoder must hand downstream a complete file. Drop rather than error out: a bad
	// thumbnail must not tear down the encode thread.
	if (_codec_id == cmn::MediaCodecId::Avif)
	{
		auto avif_image = avif::Packager::Pack(media_packet->GetData());
		if (avif_image == nullptr)
		{
			logtw("Could not wrap the AV1 still into an AVIF image; dropping this thumbnail frame");
			return EncodeResult::NoOutput();
		}

		media_packet->SetData(avif_image);
	}

	if (GetRefTrack()->GetMediaType() == cmn::MediaType::Audio)
	{
		// If the pts value is under zero, the dash packetizer does not work. Drop it but keep draining.
		if (media_packet->GetPts() < 0)
		{
			return EncodeResult::NoOutput();
		}
	}

#if DEBUG
	if (GetRefTrack()->GetMediaType() == cmn::MediaType::Video && media_packet->IsKeyFrame() == true)
	{
		logtt("keyframe is encoded. pts:%" PRId64 "ms, dts:%" PRId64 "ms, delta:%" PRId64 "ms",
			  static_cast<int64_t>(media_packet->GetPts() * 1000 / GetRefTrack()->GetTimeBase().GetTimescale()),
			  static_cast<int64_t>(media_packet->GetDts() * 1000 / GetRefTrack()->GetTimeBase().GetTimescale()),
			  static_cast<int64_t>(_last_keyframe_delta_time * 1000 / GetRefTrack()->GetTimeBase().GetTimescale()));
	}
#endif

	return EncodeResult::Encoded(std::move(media_packet));
}
