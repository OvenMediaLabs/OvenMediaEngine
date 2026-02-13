//==============================================================================
//
//  Transcode
//
//  Created by Kwon Keuk Han
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================
#include "encoder_aac.h"

#include "../../transcoder_private.h"

bool EncoderAAC::SetCodecParams()
{
	_codec_context->bit_rate = GetRefTrack()->GetBitrate();
	_codec_context->sample_fmt = (AVSampleFormat)GetSupportAudioFormat();
	_codec_context->sample_rate = GetRefTrack()->GetSampleRate();
	::av_channel_layout_default(&_codec_context->ch_layout, GetRefTrack()->GetChannel().GetCounts());
	_codec_context->initial_padding = 0;

	_bitstream_format = cmn::BitstreamFormat::AAC_ADTS;;
	
	_packet_type = cmn::PacketType::RAW;

	return true;
}


bool EncoderAAC::InitCodec()
{
	const AVCodec *codec = ::avcodec_find_encoder(ffmpeg::compat::ToAVCodecId(GetCodecID()));
	if (codec == nullptr)
	{
		logte("Codec not found: %s", cmn::GetCodecIdString(GetCodecID()));
		return false;
	}

	// create codec context
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

	// open codec
	if (::avcodec_open2(_codec_context, nullptr, nullptr) < 0)
	{
		logte("Could not open codec: %s", cmn::GetCodecIdString(GetCodecID()));
		return false;
	}

	GetRefTrack()->SetAudioSamplesPerFrame(_codec_context->frame_size);

	return true;
}

bool EncoderAAC::Configure(std::shared_ptr<MediaTrack> context)
{
	if (TranscodeEncoder::Configure(context) == false)
	{
		return false;
	}

	try
	{
		_kill_flag = false;

		_codec_thread = std::thread(&EncoderAAC::CodecThread, this);
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
