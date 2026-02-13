//==============================================================================
//
//  Transcoder
//
//  Created by Kwon Keuk Han
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================
#include "encoder_hevc_xma.h"

#include <unistd.h>

#include "../../transcoder_private.h"

bool EncoderHEVCxXMA::SetCodecParams()
{
	_codec_context->framerate = ::av_d2q((GetRefTrack()->GetFrameRateByConfig() > 0) ? GetRefTrack()->GetFrameRateByConfig() : GetRefTrack()->GetFrameRateByMeasured(), AV_TIME_BASE);
	_codec_context->bit_rate = _codec_context->rc_min_rate = _codec_context->rc_max_rate = GetRefTrack()->GetBitrate();
	_codec_context->sample_aspect_ratio = ::av_make_q(1, 1);
	_codec_context->ticks_per_frame = 2;
	_codec_context->time_base = ffmpeg::compat::TimebaseToAVRational(GetRefTrack()->GetTimeBase());
	_codec_context->pix_fmt = ffmpeg::compat::ToAVPixelFormat(GetSupportVideoFormat());
	_codec_context->width = GetRefTrack()->GetWidth();
	_codec_context->height = GetRefTrack()->GetHeight();

	// Control Rate
	::av_opt_set(_codec_context->priv_data, "control-rate", "cbr", 0); // low-latency

	// Bitrate
	::av_opt_set_int(_codec_context->priv_data, "max-bitrate",  _codec_context->bit_rate, 0);

	// Keyframe Interval
	// @see transcoder_encoder.cpp / force_keyframe_by_time_interval
	auto key_frame_interval_type = GetRefTrack()->GetKeyFrameIntervalTypeByConfig();
	if (key_frame_interval_type == cmn::KeyFrameIntervalType::TIME)
	{
		_codec_context->gop_size = (int32_t)(GetRefTrack()->GetFrameRate() * (double)GetRefTrack()->GetKeyFrameInterval() / 1000 * 2);
	}
	else if (key_frame_interval_type == cmn::KeyFrameIntervalType::FRAME)
	{
		_codec_context->gop_size = (GetRefTrack()->GetKeyFrameInterval() == 0) ? (_codec_context->framerate.num / _codec_context->framerate.den) : GetRefTrack()->GetKeyFrameInterval();
		::av_opt_set_int(_codec_context->priv_data, "periodicity-idr",  _codec_context->gop_size, 0);		
	}

	// Bframes
	::av_opt_set_int(_codec_context->priv_data, "bf", GetRefTrack()->GetBFrames(), 0);

	// Lookahead (1 ~ 30)
	if (GetRefTrack()->GetLookaheadByConfig() >= 0)
	{
#if 0
		av_opt_set_int(_codec_context->priv_data, "lookahead_depth", GetRefTrack()->GetLookaheadByConfig(), 0);
		// ::av_opt_set(_codec_context->priv_data, "expert-options", "lookahead-rc-off=0", 0);
#else
		logtw("Xilinx encoder does not support Lookahead yet.");		
#endif	
	}

	// Profile
	auto profile = GetRefTrack()->GetProfile();
	if (profile == "main" || profile.IsEmpty() == true)
	{
		::av_opt_set(_codec_context->priv_data, "profile", "main", 0);
	}
	else if (profile == "main-intra")
	{
		::av_opt_set(_codec_context->priv_data, "profile", "main-intra", 0);
	}
	else
	{
		logtw("This is an unknown profile. change to the default(baseline) profile.");
		::av_opt_set(_codec_context->priv_data, "profile", "main", 0);
	}

	::av_opt_set(_codec_context->priv_data, "level", "4.1", 0);
	::av_opt_set(_codec_context->priv_data, "tier", "main", 0);
	::av_opt_set(_codec_context->priv_data, "scaling-list", "flat", 0);

	::av_opt_set_int(_codec_context->priv_data, "lxlnx_hwdev", GetRefTrack()->GetCodecDeviceId(), 0);

	auto preset = GetRefTrack()->GetPreset().LowerCaseString();
	if (preset.IsEmpty() == false)
	{
		logtt("Xilinx encoder does not support preset");
	}	

	_bitstream_format = cmn::BitstreamFormat::H265_ANNEXB;
	
	_packet_type = cmn::PacketType::NALU;

	return true;
}

bool EncoderHEVCxXMA::InitCodec()
{
	const AVCodec *codec = ::avcodec_find_encoder_by_name("mpsoc_vcu_hevc");
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

bool EncoderHEVCxXMA::Configure(std::shared_ptr<MediaTrack> context)
{
	if (TranscodeEncoder::Configure(context) == false)
	{
		return false;
	}

	try
	{
		_kill_flag = false;

		_codec_thread = std::thread(&EncoderHEVCxXMA::CodecThread, this);
		pthread_setname_np(_codec_thread.native_handle(), ov::String::FormatString("ENC-%sxa-t%d", cmn::GetCodecIdString(GetCodecID()), _track->GetId()).CStr());
		
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
