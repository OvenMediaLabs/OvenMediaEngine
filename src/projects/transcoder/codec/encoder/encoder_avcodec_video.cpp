//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Keukhan
//  Copyright (c) 2026 OvenMediaLabs. All rights reserved.
//
//==============================================================================
#include "encoder_avcodec_video.h"

#include <unistd.h>

#include <algorithm>
#include <thread>

#include "../../transcoder_private.h"


AVCodecVideoEncoder::~AVCodecVideoEncoder()
{
	Uninitialize();

}

// ---------------------------------------------------------------------------
// libx264
// ---------------------------------------------------------------------------
bool AVCodecVideoEncoder::SetParamsX264()
{
	_codec.SetBitrate(GetRefTrack()->GetBitrate());
	_codec.SetRcMinRate(_codec.GetBitrate());
	_codec.SetRcMaxRate(_codec.GetBitrate());
	_codec.SetRcBufferSize(static_cast<int>(_codec.GetBitrate() / 2));
	_codec.SetFrameRate(cmn::Rational::FromDouble((GetRefTrack()->GetFrameRateByConfig() > 0) ? GetRefTrack()->GetFrameRateByConfig() : GetRefTrack()->GetFrameRateByMeasured()));
	_codec.SetSampleAspectRatio(cmn::Rational(1, 1));
	_codec.SetTicksPerFrame(2);
	_codec.SetTimeBase(GetRefTrack()->GetTimeBase());
	_codec.SetMaxBFrames(GetRefTrack()->GetBFrames());
	_codec.SetPixelFormat(GetSupportVideoFormat());
	auto resolution = GetRefTrack()->GetResolution();
	_codec.SetWidth(resolution.width);
	_codec.SetHeight(resolution.height);

	auto key_frame_interval_type = GetRefTrack()->GetKeyFrameIntervalTypeByConfig();
	if (key_frame_interval_type == cmn::KeyFrameIntervalType::TIME)
	{
		_codec.SetGopSize((int32_t)(GetRefTrack()->GetFrameRate() * (double)GetRefTrack()->GetKeyFrameInterval() / 1000 * 2));
	}
	else if (key_frame_interval_type == cmn::KeyFrameIntervalType::FRAME)
	{
		_codec.SetGopSize((GetRefTrack()->GetKeyFrameInterval() == 0) ? (_codec.GetFrameRate().num / _codec.GetFrameRate().den) : GetRefTrack()->GetKeyFrameInterval());
	}

	_codec.SetThreadCount(GetRefTrack()->GetThreadCount() < 0 ? std::min(std::max(4, static_cast<int>(std::max(1u, std::thread::hardware_concurrency())) / 3), 8) : GetRefTrack()->GetThreadCount());

	if (GetRefTrack()->GetLookaheadByConfig() >= 0)
	{
		_codec.SetOption("rc-lookahead", GetRefTrack()->GetLookaheadByConfig());
	}

	auto profile = GetRefTrack()->GetProfile();
	if (profile.IsEmpty() == true)
	{
		_codec.SetOption("profile", "baseline");
	}
	else if (profile == "baseline")
	{
		_codec.SetOption("profile", "baseline");
	}
	else if (profile == "main")
	{
		_codec.SetOption("profile", "main");
	}
	else if (profile == "high")
	{
		_codec.SetOption("profile", "high");
	}
	else
	{
		logtw("This is an unknown profile. change to the default(baseline) profile.");
		_codec.SetOption("profile", "baseline");
	}

	auto preset = GetRefTrack()->GetPreset();
	if (preset == "slower")
	{
		_codec.SetOption("preset", "slower");
	}
	else if (preset == "slow")
	{
		_codec.SetOption("preset", "slow");
	}
	else if (preset == "medium")
	{
		_codec.SetOption("preset", "medium");
	}
	else if (preset == "fast")
	{
		_codec.SetOption("preset", "fast");
	}
	else if (preset == "faster")
	{
		_codec.SetOption("preset", "faster");
	}
	else
	{
		_codec.SetOption("preset", "faster");
	}

	_codec.SetOption("tune", "zerolatency");

	// Do not use 'sliced-threads' option from encoding delay. Can't be compatible with macOS environment.
	ov::String extra_options = ov::String::FormatString(
		"bframes=%d:sliced-threads=0:b-adapt=1:no-scenecut:keyint=%d:min-keyint=%d",
		GetRefTrack()->GetBFrames(), _codec.GetGopSize(), _codec.GetGopSize());

	if (!GetRefTrack()->GetExtraEncoderOptionsByConfig().IsEmpty())
	{
		if (!GetRefTrack()->GetExtraEncoderOptionsByConfig().HasPrefix(":"))
			extra_options += ":";

		extra_options += GetRefTrack()->GetExtraEncoderOptionsByConfig();
	}
	_codec.SetOption("x264opts", extra_options.CStr());

	logtd("opts: %s", ffmpeg::compat::GetAVOptionsString(_codec.GetPrivData()).CStr());

	_bitstream_format = cmn::BitstreamFormat::H264_ANNEXB;
	_packet_type = cmn::PacketType::NALU;

	return true;
}

// ---------------------------------------------------------------------------
// libopenh264
// ---------------------------------------------------------------------------
bool AVCodecVideoEncoder::SetParamsOpenH264()
{
	_codec.SetFrameRate(cmn::Rational::FromDouble((GetRefTrack()->GetFrameRateByConfig() > 0) ? GetRefTrack()->GetFrameRateByConfig() : GetRefTrack()->GetFrameRateByMeasured()));
	_codec.SetBitrate(GetRefTrack()->GetBitrate());
	_codec.SetRcMinRate(_codec.GetBitrate());
	_codec.SetRcMaxRate(_codec.GetBitrate());
	_codec.SetSampleAspectRatio(cmn::Rational(1, 1));
	_codec.SetTicksPerFrame(2);
	_codec.SetTimeBase(GetRefTrack()->GetTimeBase());
	_codec.SetPixelFormat(GetSupportVideoFormat());
	auto resolution = GetRefTrack()->GetResolution();
	_codec.SetWidth(resolution.width);
	_codec.SetHeight(resolution.height);

	// The openh264 encoder does not generate keyframes consistently.
	auto key_frame_interval_type = GetRefTrack()->GetKeyFrameIntervalTypeByConfig();
	if (key_frame_interval_type == cmn::KeyFrameIntervalType::TIME)
	{
		_codec.SetGopSize(0);
	}
	else if (key_frame_interval_type == cmn::KeyFrameIntervalType::FRAME)
	{
		_codec.SetGopSize((GetRefTrack()->GetKeyFrameInterval() == 0) ? (_codec.GetFrameRate().num / _codec.GetFrameRate().den) : GetRefTrack()->GetKeyFrameInterval());
	}

	_codec.SetThreadCount(GetRefTrack()->GetThreadCount() < 0 ? std::min(std::max(4, static_cast<int>(std::max(1u, std::thread::hardware_concurrency())) / 3), 8) : GetRefTrack()->GetThreadCount());
	_codec.SetSlices(_codec.GetThreadCount());

	_codec.SetOption("coder", "default");
	_codec.SetOption("allow_skip_frames", "false");

	if (GetRefTrack()->GetLookaheadByConfig() >= 0)
	{
		logtw("Lookahead is not supported in OpenH264.");
	}

	auto profile = GetRefTrack()->GetProfile();
	if (profile.IsEmpty() == true)
	{
		_codec.SetOption("profile", "constrained_baseline");
	}
	else if (profile == "baseline")
	{
		_codec.SetOption("profile", "constrained_baseline");
	}
	else if (profile == "high")
	{
		_codec.SetOption("profile", "high");
	}
	else
	{
		if (profile == "main")
			logtw("OpenH264 does not support the main profile. The main profile is changed to the baseline profile.");
		else
			logtw("This is an unknown profile. change to the default(baseline) profile.");

		_codec.SetOption("profile", "constrained_baseline");
	}

	_codec.SetOption("loopfilter", 1);

	auto preset = GetRefTrack()->GetPreset().LowerCaseString();
	if (preset.IsEmpty() == true)
	{
		_codec.SetOption("rc_mode", "bitrate");
	}
	else
	{
		logtt("If the preset is used in the openh264 codec, constant bitrate is not supported");

		_codec.SetOption("rc_mode", "quality");

		if (preset == "slower")
		{
			_codec.SetQMin(10);
			_codec.SetQMax(39);
		}
		else if (preset == "slow")
		{
			_codec.SetQMin(16);
			_codec.SetQMax(45);
		}
		else if (preset == "medium")
		{
			_codec.SetQMin(24);
			_codec.SetQMax(51);
		}
		else if (preset == "fast")
		{
			_codec.SetQMin(32);
			_codec.SetQMax(51);
		}
		else if (preset == "faster")
		{
			_codec.SetQMin(40);
			_codec.SetQMax(51);
		}
		else
		{
			logtw("Unknown preset: %s", preset.CStr());
		}
	}

	_bitstream_format = cmn::BitstreamFormat::H264_ANNEXB;
	_packet_type = cmn::PacketType::NALU;

	return true;
}

// ---------------------------------------------------------------------------
// libvpx (VP8)
// ---------------------------------------------------------------------------
bool AVCodecVideoEncoder::SetParamsVp8()
{
	_codec.SetBitrate(GetRefTrack()->GetBitrate());
	_codec.SetRcMaxRate(_codec.GetBitrate());
	_codec.SetRcMinRate(_codec.GetBitrate());
	_codec.SetSampleAspectRatio(cmn::Rational(1, 1));
	_codec.SetTimeBase(GetRefTrack()->GetTimeBase());
	_codec.SetFrameRate(cmn::Rational::FromDouble((GetRefTrack()->GetFrameRateByConfig() > 0) ? GetRefTrack()->GetFrameRateByConfig() : GetRefTrack()->GetFrameRateByMeasured()));
	_codec.SetMaxBFrames(0);
	_codec.SetPixelFormat(GetSupportVideoFormat());
	auto resolution = GetRefTrack()->GetResolution();
	_codec.SetWidth(resolution.width);
	_codec.SetHeight(resolution.height);

	auto key_frame_interval_type = GetRefTrack()->GetKeyFrameIntervalTypeByConfig();
	if (key_frame_interval_type == cmn::KeyFrameIntervalType::TIME)
	{
		_codec.SetGopSize((int32_t)(GetRefTrack()->GetFrameRate() * (double)GetRefTrack()->GetKeyFrameInterval() / 1000 * 2));
	}
	else if (key_frame_interval_type == cmn::KeyFrameIntervalType::FRAME)
	{
		_codec.SetGopSize((GetRefTrack()->GetKeyFrameInterval() == 0) ? (_codec.GetFrameRate().num / _codec.GetFrameRate().den) : GetRefTrack()->GetKeyFrameInterval());
	}

	_codec.SetThreadCount(GetRefTrack()->GetThreadCount() < 0 ? std::min(std::max(4, static_cast<int>(std::max(1u, std::thread::hardware_concurrency())) / 3), 8) : GetRefTrack()->GetThreadCount());

	auto preset = GetRefTrack()->GetPreset().LowerCaseString();
	if (preset.IsEmpty() == true)
	{
		_codec.SetOption("quality", "realtime");
	}
	else if (preset == "slower" || preset == "slow")
	{
		_codec.SetOption("quality", "best");
	}
	else if (preset == "medium")
	{
		_codec.SetOption("quality", "good");
	}
	else if (preset == "fast" || preset == "faster")
	{
		_codec.SetOption("quality", "realtime");
	}
	else
	{
		logtw("Unknown preset: %s", preset.CStr());
	}

	_bitstream_format = cmn::BitstreamFormat::VP8;
	_packet_type = cmn::PacketType::RAW;

	return true;
}

bool AVCodecVideoEncoder::OpenCodec()
{
	bool allocated = false;

	if (_codec_id == cmn::MediaCodecId::Vp8)
	{
		allocated = _codec.AllocEncoder(GetCodecID());
	}
	else if (_module_id == cmn::MediaCodecModuleId::X264)
	{
		allocated = _codec.AllocEncoderByName("libx264");
	}
	else
	{
		// OPENH264 (and DEFAULT for H264)
		allocated = _codec.AllocEncoderByName("libopenh264");
	}

	if (allocated == false)
	{
		logte("Could not allocate encoder context for %s", cmn::GetCodecIdString(GetCodecID()));
		return false;
	}

	bool result = false;
	switch (_codec_id)
	{
		case cmn::MediaCodecId::Vp8:
			result = SetParamsVp8();
			break;
		default:
			if (_module_id == cmn::MediaCodecModuleId::X264)
			{
				result = SetParamsX264();
			}
			else
			{
				result = SetParamsOpenH264();
			}
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

bool AVCodecVideoEncoder::Initialize()
{
	auto result = OpenCodec();
	if (_track != nullptr)
	{
		_track->SetCodecStatus(result ? cmn::CodecStatus::Ready : cmn::CodecStatus::Failed);
	}

	return result;
}

void AVCodecVideoEncoder::Uninitialize()
{
	_codec.Flush();
	_codec.Reset();
}

EncodeResult AVCodecVideoEncoder::SendFrame(const std::shared_ptr<const MediaFrame> &frame, bool force_keyframe)
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

EncodeResult AVCodecVideoEncoder::ReceivePacket()
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

