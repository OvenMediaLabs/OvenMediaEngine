//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Keukhan
//  Copyright (c) 2026 OvenMediaLabs. All rights reserved.
//
//==============================================================================
#include "encoder_avcodec_audio.h"

#include "../../transcoder_private.h"

bool AVCodecAudioEncoder::SetParamsAac()
{
	_codec.SetBitrate(GetRefTrack()->GetBitrate());
	_codec.SetSampleFormat(GetSupportAudioFormat());
	_codec.SetSampleRate(GetRefTrack()->GetSampleRate());
	_codec.SetDefaultChannelLayout(GetRefTrack()->GetChannel().GetCounts());
	_codec.SetInitialPadding(0);

	_bitstream_format = cmn::BitstreamFormat::AAC_ADTS;
	_packet_type = cmn::PacketType::RAW;

	return true;
}

bool AVCodecAudioEncoder::SetParamsOpus()
{
	_codec.SetBitrate(GetRefTrack()->GetBitrate());
	_codec.SetSampleFormat(GetSupportAudioFormat());
	_codec.SetSampleRate(GetRefTrack()->GetSampleRate());
	_codec.SetDefaultChannelLayout(GetRefTrack()->GetChannel().GetCounts());

	// Compression Level (0~10): 0 fast/low quality, 10 slow/high quality.
	_codec.SetCompressionLevel(10);

	_codec.SetOption("application", "lowdelay");
	_codec.SetOption("frame_duration", "20.0");
	_codec.SetOption("packet_loss", "10");
	_codec.SetOption("vbr", "off");

	_bitstream_format = cmn::BitstreamFormat::OPUS;
	_packet_type = cmn::PacketType::RAW;

	return true;
}

bool AVCodecAudioEncoder::OpenCodec()
{
	if (_codec.AllocEncoder(GetCodecID()) == false)
	{
		logte("Could not allocate encoder context for %s", cmn::GetCodecIdString(GetCodecID()));
		return false;
	}

	bool result = false;
	switch (_codec_id)
	{
		case cmn::MediaCodecId::Opus:
			result = SetParamsOpus();
			break;
		case cmn::MediaCodecId::Aac:
		default:
			result = SetParamsAac();
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

	GetRefTrack()->SetAudioSamplesPerFrame(_codec.GetFrameSize());

	return true;
}

bool AVCodecAudioEncoder::Initialize()
{
	auto result = OpenCodec();
	if (_track != nullptr)
	{
		_track->SetCodecStatus(result ? cmn::CodecStatus::Ready : cmn::CodecStatus::Failed);
	}

	return result;
}

void AVCodecAudioEncoder::Uninitialize()
{
	_codec.Flush();
	_codec.Reset();
}

EncodeResult AVCodecAudioEncoder::SendFrame(const std::shared_ptr<const MediaFrame> &frame, bool force_keyframe)
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

EncodeResult AVCodecAudioEncoder::ReceivePacket()
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
