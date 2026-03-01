//==============================================================================
//
//  Transcode
//
//  Created by Kwon Keuk Han
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================
#include "encoder_vp8.h"

#include "../../transcoder_private.h"

bool EncoderVP8::SetCodecParams()
{
	_codec_context->bit_rate = GetRefTrack()->GetBitrate();
	_codec_context->rc_max_rate = _codec_context->bit_rate;
	_codec_context->rc_min_rate = _codec_context->bit_rate;
	_codec_context->sample_aspect_ratio = (AVRational){1, 1};
	_codec_context->time_base = ffmpeg::compat::TimebaseToAVRational(GetRefTrack()->GetTimeBase());
	_codec_context->framerate = ::av_d2q((GetRefTrack()->GetFrameRateByConfig() > 0) ? GetRefTrack()->GetFrameRateByConfig() : GetRefTrack()->GetFrameRateByMeasured(), AV_TIME_BASE);
	_codec_context->max_b_frames = 0;
	_codec_context->pix_fmt = ffmpeg::compat::ToAVPixelFormat(GetSupportVideoFormat());
	auto resolution = GetRefTrack()->GetResolution();
	_codec_context->width = resolution.width;
	_codec_context->height = resolution.height;

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
	}
	// VP8 does not support bframe

	// -1(Default) => FFMIN(FFMAX(4, av_cpu_count() / 3), 8) 
	// 0 => Auto
	// >1 => Set
	_codec_context->thread_count = GetRefTrack()->GetThreadCount() < 0 ? FFMIN(FFMAX(4, av_cpu_count() / 3), 8) : GetRefTrack()->GetThreadCount();

	// Preset
	auto preset = GetRefTrack()->GetPreset().LowerCaseString();
	if (preset.IsEmpty() == true)
	{
		::av_opt_set(_codec_context->priv_data, "quality", "realtime", 0);
	}
	else
	{
		if (preset == "slower" || preset == "slow")
		{
			::av_opt_set(_codec_context->priv_data, "quality", "best", 0);
		}
		else if (preset == "medium")
		{
			::av_opt_set(_codec_context->priv_data, "quality", "good", 0);

		}		
		else if (preset == "fast" || preset == "faster")
		{
			::av_opt_set(_codec_context->priv_data, "quality", "realtime", 0);
		}
		else{
			logtw("Unknown preset: %s", preset.CStr());
		}
	}

	_bitstream_format = cmn::BitstreamFormat::VP8;
	
	_packet_type = cmn::PacketType::RAW;	

	return true;
}

bool EncoderVP8::InitCodec()
{
	const AVCodec *codec = ::avcodec_find_encoder(ffmpeg::compat::ToAVCodecId(GetCodecID()));
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
		logte("Could not open codec");
		return false;
	}

	return true;
}

bool EncoderVP8::Configure(std::shared_ptr<MediaTrack> context)
{
	if (TranscodeEncoder::Configure(context) == false)
	{
		return false;
	}

	try
	{
		_kill_flag = false;

		_codec_thread = std::thread(&EncoderVP8::CodecThread, this);
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

