//==============================================================================
//
//  Transcode
//
//  Created by Kwon Keuk Han
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================
#include "encoder_ffopus.h"

#include "../../transcoder_private.h"

bool EncoderFFOPUS::SetCodecParams()
{
	_codec_context->bit_rate = GetRefTrack()->GetBitrate();
	_codec_context->sample_fmt = (AVSampleFormat)GetSupportAudioFormat();
	_codec_context->sample_rate = GetRefTrack()->GetSampleRate();
	::av_channel_layout_default(&_codec_context->ch_layout, GetRefTrack()->GetChannel().GetCounts());
	
	// Support Frequency
	// 4000 - OPUS_BANDWIDTH_NARROWBAND (8kHz) (2~8 kbps)
	// 6000 - OPUS_BANDWIDTH_MEDIUMBAND (12kHz)
	// 8000 - OPUS_BANDWIDTH_WIDEBAND (16kHz) (9~12 kbps)
	// 12000 - OPUS_BANDWIDTH_SUPERWIDEBAND (24kHz)
	// 20000 - OPUS_BANDWIDTH_FULLBAND (48kHz) (13~2048 kbps)
	// _codec_context->cutoff = 20000;	 

	// Compression Level (0~10)
	// 0 Low quality and fast encoding (less CPU usage)
	// 10 - High quality, slow encoding (high CPU usage)
	_codec_context->compression_level = 10;

	::av_opt_set(_codec_context->priv_data, "application", "lowdelay", 0);
	::av_opt_set(_codec_context->priv_data, "frame_duration", "20.0", 0);
	::av_opt_set(_codec_context->priv_data, "packet_loss", "10", 0);
	::av_opt_set(_codec_context->priv_data, "vbr", "off", 0);

	_bitstream_format = cmn::BitstreamFormat::OPUS;
	
	_packet_type = cmn::PacketType::RAW;

	return true;
}

bool EncoderFFOPUS::InitCodec()
{
	const AVCodec *codec = ::avcodec_find_encoder(ffmpeg::compat::ToAVCodecId(GetCodecID()));
	if (codec == nullptr)
	{
		logte("Codec not found: %s", cmn::GetCodecIdString(GetCodecID()));
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

	GetRefTrack()->SetAudioSamplesPerFrame(_codec_context->frame_size);

	return true;
}

bool EncoderFFOPUS::Configure(std::shared_ptr<MediaTrack> output_context)
{
	if (TranscodeEncoder::Configure(output_context) == false)
	{
		return false;
	}

	try
	{
		_kill_flag = false;

		_codec_thread = std::thread(&EncoderFFOPUS::CodecThread, this);
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

