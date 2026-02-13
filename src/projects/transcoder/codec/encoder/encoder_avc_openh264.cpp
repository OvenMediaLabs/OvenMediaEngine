//==============================================================================
//
//  Transcoder
//
//  Created by Kwon Keuk Han
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================
#include "encoder_avc_openh264.h"

#include <unistd.h>

#include "../../transcoder_private.h"

bool EncoderAVCxOpenH264::SetCodecParams()
{
	_codec_context->framerate = ::av_d2q((GetRefTrack()->GetFrameRateByConfig() > 0) ? GetRefTrack()->GetFrameRateByConfig() : GetRefTrack()->GetFrameRateByMeasured(), AV_TIME_BASE);
	_codec_context->bit_rate = _codec_context->rc_min_rate = _codec_context->rc_max_rate = GetRefTrack()->GetBitrate();
	_codec_context->sample_aspect_ratio = ::av_make_q(1, 1);
	_codec_context->ticks_per_frame = 2;
	_codec_context->time_base = ffmpeg::compat::TimebaseToAVRational(GetRefTrack()->GetTimeBase());
	_codec_context->pix_fmt = ffmpeg::compat::ToAVPixelFormat(GetSupportVideoFormat());
	_codec_context->width = GetRefTrack()->GetWidth();
	_codec_context->height = GetRefTrack()->GetHeight();

	// Keyframe Interval
	// @see transcoder_encoder.cpp / force_keyframe_by_time_interval
	// The openh264 encoder does not generate keyframes consistently.	
	auto key_frame_interval_type = GetRefTrack()->GetKeyFrameIntervalTypeByConfig();
	if (key_frame_interval_type == cmn::KeyFrameIntervalType::TIME)
	{
		_codec_context->gop_size = 0; 
	}
	else if (key_frame_interval_type == cmn::KeyFrameIntervalType::FRAME)
	{
		_codec_context->gop_size = (GetRefTrack()->GetKeyFrameInterval() == 0) ? (_codec_context->framerate.num / _codec_context->framerate.den) : GetRefTrack()->GetKeyFrameInterval();
	}

	// -1(Default) => FFMIN(FFMAX(4, av_cpu_count() / 3), 8) 
	// 0 => Auto
	// >1 => Set
	_codec_context->thread_count = GetRefTrack()->GetThreadCount() < 0 ? FFMIN(FFMAX(4, av_cpu_count() / 3), 8) : GetRefTrack()->GetThreadCount();
	_codec_context->slices = _codec_context->thread_count;

	::av_opt_set(_codec_context->priv_data, "coder", "default", 0);

	// Use the high profile to remove this log.
	//  'Warning:bEnableFrameSkip = 0,bitrate can't be controlled for RC_QUALITY_MODE,RC_BITRATE_MODE and RC_TIMESTAMP_MODE without enabling skip frame'
	::av_opt_set(_codec_context->priv_data, "allow_skip_frames", "false", 0);

	// Lookahead
	if (GetRefTrack()->GetLookaheadByConfig() >= 0)
	{
		logtw("Lookahead is not supported in OpenH264.");
	}

	// Profile
	auto profile = GetRefTrack()->GetProfile();
	if (profile.IsEmpty() == true)
	{
		::av_opt_set(_codec_context->priv_data, "profile", "constrained_baseline", 0);
	}
	else
	{
		if (profile == "baseline")
		{
			::av_opt_set(_codec_context->priv_data, "profile", "constrained_baseline", 0);
		}
		else if (profile == "high")
		{
			::av_opt_set(_codec_context->priv_data, "profile", "high", 0);
		}
		else
		{
			 if (profile == "main") 
				logtw("OpenH264 does not support the main profile. The main profile is changed to the baseline profile.");
			else
				logtw("This is an unknown profile. change to the default(baseline) profile.");

			::av_opt_set(_codec_context->priv_data, "profile", "constrained_baseline", 0);
		}
	}

	// Loop Filter
	::av_opt_set_int(_codec_context->priv_data, "loopfilter", 1, 0);

	// Preset
	auto preset = GetRefTrack()->GetPreset().LowerCaseString();
	if (preset.IsEmpty() == true)
	{
		::av_opt_set(_codec_context->priv_data, "rc_mode", "bitrate", 0);
	}
	else
	{
		logtt("If the preset is used in the openh264 codec, constant bitrate is not supported");

		::av_opt_set(_codec_context->priv_data, "rc_mode", "quality", 0);

		if (preset == "slower")
		{
			_codec_context->qmin = 10;
			_codec_context->qmax = 39;
		}
		else if (preset == "slow")
		{
			_codec_context->qmin = 16;
			_codec_context->qmax = 45;
		}
		else if (preset == "medium")
		{
			_codec_context->qmin = 24;
			_codec_context->qmax = 51;
		}		
		else if (preset == "fast")
		{
			_codec_context->qmin = 32;
			_codec_context->qmax = 51;
		}
		else if (preset == "faster")
		{
			_codec_context->qmin = 40;
			_codec_context->qmax = 51;
		}		
		else{
			logtw("Unknown preset: %s", preset.CStr());
		}
	}

	_bitstream_format = cmn::BitstreamFormat::H264_ANNEXB;
	
	_packet_type = cmn::PacketType::NALU;

	return true;
}

bool EncoderAVCxOpenH264::InitCodec()
{
	const AVCodec *codec = ::avcodec_find_encoder_by_name("libopenh264");
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

bool EncoderAVCxOpenH264::Configure(std::shared_ptr<MediaTrack> context)
{
	if (TranscodeEncoder::Configure(context) == false)
	{
		return false;
	}

	try
	{
		_kill_flag = false;

		_codec_thread = std::thread(&EncoderAVCxOpenH264::CodecThread, this);
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
