//==============================================================================
//
//  Transcoder
//
//  Created by Kwon Keuk Han
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================
#include "encoder_avc_x264.h"

#include <unistd.h>

#include "../../transcoder_private.h"


bool EncoderAVCx264::SetCodecParams()
{
	_codec_context->bit_rate = GetRefTrack()->GetBitrate();
	_codec_context->rc_min_rate = _codec_context->rc_max_rate = _codec_context->bit_rate;
	_codec_context->rc_buffer_size = static_cast<int>(_codec_context->bit_rate / 2);
	_codec_context->framerate = ::av_d2q((GetRefTrack()->GetFrameRateByConfig() > 0) ? GetRefTrack()->GetFrameRateByConfig() : GetRefTrack()->GetFrameRateByMeasured(), AV_TIME_BASE);
	_codec_context->sample_aspect_ratio = ::av_make_q(1, 1);
	
	// From avcodec.h:
	// For some codecs, the time base is closer to the field rate than the frame rate.
	// Most notably, H.264 and MPEG-2 specify time_base as half of frame duration
	// if no telecine is used ...
	// Set to time_base ticks per frame. Default 1, e.g., H.264/MPEG-2 set it to 2.
	_codec_context->ticks_per_frame = 2;

	// From avcodec.h:
	// For fixed-fps content, timebase should be 1/framerate and timestamp increments should be identically 1.
	// This often, but not always is the inverse of the frame rate or field rate for video. 1/time_base is not the average frame rate if the frame rate is not constant.
	_codec_context->time_base = (AVRational){GetRefTrack()->GetTimeBase().GetNum(), GetRefTrack()->GetTimeBase().GetDen()};
	_codec_context->max_b_frames = GetRefTrack()->GetBFrames();
	_codec_context->pix_fmt = ffmpeg::compat::ToAVPixelFormat(GetSupportVideoFormat());
	_codec_context->width = GetRefTrack()->GetWidth();
	_codec_context->height = GetRefTrack()->GetHeight();

	// Keyframe Interval
	//@see transcoder_encoder.cpp / force_keyframe_by_time_interval
	auto key_frame_interval_type = GetRefTrack()->GetKeyFrameIntervalTypeByConfig();
	if (key_frame_interval_type == cmn::KeyFrameIntervalType::TIME)
	{
		_codec_context->gop_size = (int32_t)(GetRefTrack()->GetFrameRate() * (double)GetRefTrack()->GetKeyFrameInterval() / 1000 * 2);
	}
	else if (key_frame_interval_type == cmn::KeyFrameIntervalType::FRAME)
	{
		_codec_context->gop_size = (GetRefTrack()->GetKeyFrameInterval() == 0) ? (_codec_context->framerate.num / _codec_context->framerate.den) : GetRefTrack()->GetKeyFrameInterval();
	}

	// -1(Default) => FFMIN(FFMAX(4, av_cpu_count() / 3), 8) 
	// 0 => Auto
	// >1 => Set
	_codec_context->thread_count = GetRefTrack()->GetThreadCount() < 0 ? FFMIN(FFMAX(4, av_cpu_count() / 3), 8) : GetRefTrack()->GetThreadCount();

	// Lookahead
	if (GetRefTrack()->GetLookaheadByConfig() >= 0)
	{
		av_opt_set_int(_codec_context->priv_data, "rc-lookahead", GetRefTrack()->GetLookaheadByConfig(), 0);
	}

	// Profile
	auto profile = GetRefTrack()->GetProfile();
	if (profile.IsEmpty() == true)
	{
		::av_opt_set(_codec_context->priv_data, "profile", "baseline", 0);
	}
	else
	{
		if (profile == "baseline")
		{
			::av_opt_set(_codec_context->priv_data, "profile", "baseline", 0);
		}
		else if (profile == "main")
		{
			::av_opt_set(_codec_context->priv_data, "profile", "main", 0);
		}
		else if (profile == "high")
		{
			::av_opt_set(_codec_context->priv_data, "profile", "high", 0);
		}
		else
		{
			logtw("This is an unknown profile. change to the default(baseline) profile.");
			::av_opt_set(_codec_context->priv_data, "profile", "baseline", 0);
		}
	}

	// Preset
	if (GetRefTrack()->GetPreset() == "slower")
	{
		::av_opt_set(_codec_context->priv_data, "preset", "slower", 0);
	}
	else if (GetRefTrack()->GetPreset() == "slow")
	{
		::av_opt_set(_codec_context->priv_data, "preset", "slow", 0);
	}
	else if (GetRefTrack()->GetPreset() == "medium")
	{
		::av_opt_set(_codec_context->priv_data, "preset", "medium", 0);
	}
	else if (GetRefTrack()->GetPreset() == "fast")
	{
		::av_opt_set(_codec_context->priv_data, "preset", "fast", 0);
	}
	else if (GetRefTrack()->GetPreset() == "faster")
	{
		::av_opt_set(_codec_context->priv_data, "preset", "faster", 0);
	}
	else
	{
		// Default
		::av_opt_set(_codec_context->priv_data, "preset", "faster", 0);
	}

	// Tune
	::av_opt_set(_codec_context->priv_data, "tune", "zerolatency", 0);

	// Remove the sliced-thread option from encoding delay. Browser compatibility in MAC environment
	::av_opt_set(_codec_context->priv_data, "x264opts",
				 ov::String::FormatString(
					 "bframes=%d:sliced-threads=0:b-adapt=1:no-scenecut:keyint=%d:min-keyint=%d",
					 GetRefTrack()->GetBFrames(),
					 _codec_context->gop_size,
					 _codec_context->gop_size)
					 .CStr(),
				 0);

	// Do not use 'sliced-threads' option from encoding delay. Can't not compatible with macOS evironment.
	ov::String extra_options = ov::String::FormatString(
		"bframes=%d:sliced-threads=0:b-adapt=1:no-scenecut:keyint=%d:min-keyint=%d",
		GetRefTrack()->GetBFrames(), _codec_context->gop_size, _codec_context->gop_size);

	// If user has defined extra options, append them.
	if (!GetRefTrack()->GetExtraEncoderOptionsByConfig().IsEmpty())
	{
		if (!GetRefTrack()->GetExtraEncoderOptionsByConfig().HasPrefix(":"))
			extra_options += ":";

		extra_options += GetRefTrack()->GetExtraEncoderOptionsByConfig();
	}
	::av_opt_set(_codec_context->priv_data, "x264opts", extra_options.CStr(), 0);

	logtd("opts: %s", ffmpeg::compat::GetAVOptionsString(_codec_context->priv_data).CStr());

	_bitstream_format = cmn::BitstreamFormat::H264_ANNEXB;
	
	_packet_type = cmn::PacketType::NALU;

	return true;
}

// Notes.
//
// - B-frame must be disabled. because, WEBRTC does not support B-Frame.
//
bool EncoderAVCx264::InitCodec()
{
	const AVCodec *codec = ::avcodec_find_encoder_by_name("libx264");
	if (codec == nullptr)
	{
		logte("Could not find encoder: %s", cmn::GetCodecIdString(GetCodecID()));
		return false;
	}

	_codec_context = ::avcodec_alloc_context3(codec);
	if (_codec_context == nullptr)
	{
		logte("Could not allocate codec context for %s", cmn::GetCodecIdString(GetCodecID()));
		return false;
	}

	if (SetCodecParams() == false)
	{
		logte("Could not set codec parameters for %s", cmn::GetCodecIdString(GetCodecID()));
		return false;
	}

	if (::avcodec_open2(_codec_context, nullptr, nullptr) < 0)
	{
		logte("Could not open codec: %s", cmn::GetCodecIdString(GetCodecID()));
		return false;
	}

	return true;
}

bool EncoderAVCx264::Configure(std::shared_ptr<MediaTrack> context)
{
	if (TranscodeEncoder::Configure(context) == false)
	{
		return false;
	}

	try
	{
		_kill_flag = false;

		_codec_thread = std::thread(&EncoderAVCx264::CodecThread, this);
		pthread_setname_np(_codec_thread.native_handle(), ov::String::FormatString("ENC-%s-t%d", cmn::GetCodecIdString(GetCodecID()), _track->GetId()).CStr());
		
		// Initialize the codec and wait for completion.
		if(_codec_init_event.Get() == false)
		{
			_kill_flag = true;
			return false;
		}
	}
	catch (const std::system_error &e)
	{
		_kill_flag = true;
		return false;
	}

	return true;
}
