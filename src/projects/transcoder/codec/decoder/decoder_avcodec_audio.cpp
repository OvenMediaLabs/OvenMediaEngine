//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Keukhan
//  Copyright (c) 2026 OvenMediaLabs. All rights reserved.
//
//==============================================================================
#include "decoder_avcodec_audio.h"

#include "../../transcoder_private.h"
#include "base/info/application.h"

bool AVCodecAudioDecoder::Initialize()
{
	if (_analyzer.IsValid() == false)
	{
		if (_analyzer.Init(GetCodecID()) == false)
		{
			logte("Bitstream parser not found");
			return false;
		}
	}

	const char *decoder_name = nullptr;
	switch (GetCodecID())
	{
		case cmn::MediaCodecId::Aac:
			decoder_name = "aac";
			break;
		case cmn::MediaCodecId::Mp3:
			decoder_name = "mp3";
			break;
		case cmn::MediaCodecId::Mp2:
			decoder_name = "mp2";
			break;
		case cmn::MediaCodecId::Opus:
			decoder_name = "opus";
			break;
		default:
			logte("Unsupported codec for audio decoder: %s", cmn::GetCodecIdString(GetCodecID()));
			return false;
	}

	if (_codec.AllocDecoderByName(decoder_name) == false)
	{
		logte("Could not allocate decoder context for %s", cmn::GetCodecIdString(GetCodecID()));
		return false;
	}

	_codec.SetTimeBase(GetTimebase());

	if (_codec.Open() == false)
	{
		logte("Could not open decoder(%s). %s", cmn::GetCodecIdString(GetCodecID()), _codec.GetLastErrorString().CStr());
		return false;
	}


	_change_format = false;

	return true;
}

std::shared_ptr<MediaPacket> AVCodecAudioDecoder::GetFramedPacket()
{
	auto obj = _input_buffer.Dequeue();
	if (obj.has_value() == false)
	{
		return nullptr;
	}

	auto media_packet = std::move(obj.value());

	if (_analyzer.Analyze(media_packet) == false)
	{
		logte("[%s] Could not analyze the bitstream packet", _stream_info.GetUri().CStr());
		return nullptr;
	}

	return std::const_pointer_cast<MediaPacket>(media_packet);
}

DecodeResult AVCodecAudioDecoder::SendPacket(const std::shared_ptr<MediaPacket> &packet)
{
	DecodeResult out;

	auto result = _codec.SendPacket(packet);
	if (result == ffmpeg::CodecResult::Again)
	{
		// This is not an error; it just means the codec needs more input or has no output ready yet.
		out = DecodeResult::NoOutput();
	}
	else if (result == ffmpeg::CodecResult::InvalidData)
	{
		logtd("[%s] Invalid data while sending a packet for decoding. track(%u), pts(%" PRId64 ")",
			  _stream_info.GetUri().CStr(), GetRefTrack()->GetId(), packet->GetPts());

		auto empty_frame = MediaFrame::Create(cmn::MediaType::Audio, packet->GetDts());
		return DecodeResult::InvalidData(std::move(empty_frame));
	}
	else if (result != ffmpeg::CodecResult::Ok)
	{
		logte("Error occurred while sending a packet for decoding. reason(%s)", _codec.GetLastErrorString().CStr());
		out = DecodeResult::Error(_codec.GetLastErrorString());
	}

	// Save the first packet's PTS.
	if (_first_pkt_pts == INT64_MIN)
	{
		_first_pkt_pts = packet->GetPts();
	}

	return out;
}

DecodeResult AVCodecAudioDecoder::ReceiveFrame()
{
	auto received = _codec.ReceiveFrame();
	if (received.result == ffmpeg::CodecResult::Again)
	{
		return DecodeResult::NoOutput();
	}
	else if (received.result == ffmpeg::CodecResult::InvalidData)
	{
		logtw("Invalid data while receiving a packet for decoding");
		auto empty_frame = MediaFrame::Create(cmn::MediaType::Audio, _analyzer.GetDts());
		return DecodeResult::InvalidData(std::move(empty_frame));
	}
	else if (received.result != ffmpeg::CodecResult::Ok)
	{
		logte("Error receiving a packet for decoding. reason(%s)", _codec.GetLastErrorString().CStr());
		return DecodeResult::Error(_codec.GetLastErrorString());
	}

	auto output_frame = received.frame;
	if (output_frame == nullptr)
	{
		return DecodeResult::NoOutput();
	}

	bool format_changed = (_change_format == false);
	if (format_changed == true)
	{
		auto codec_info = _codec.GetCodecInfoString();
		logtd("[%s(%u)] Changed format. %s", _stream_info.GetUri().CStr(), _stream_info.GetId(), codec_info.CStr());
		_change_format = true;
	}

	// The actual duration is calculated based on the number of samples in the decoded frame.
	const int32_t timebase_den = GetRefTrack()->GetTimeBase().GetDen();
	if (timebase_den != 0 && output_frame->GetSampleRate() != 0)
	{
		output_frame->SetDuration((int64_t)(((double)output_frame->GetNbSamples() * (double)timebase_den) / (double)output_frame->GetSampleRate()));
	}

	// If the decoded audio frame has no PTS, derive it from the previous frame.
	if (output_frame->GetPts() == -1LL)
	{
		if (_last_pkt_pts == INT64_MIN)
		{
			output_frame->SetPts(_first_pkt_pts);
		}
		else
		{
			output_frame->SetPts(_last_pkt_pts + _last_pkt_duration);
		}
	}

	_last_pkt_pts = output_frame->GetPts();
	_last_pkt_duration = output_frame->GetDuration();

	return DecodeResult::Decoded(std::move(output_frame), format_changed);
}

void AVCodecAudioDecoder::Uninitialize()
{
	_codec.Flush();
	_codec.Reset();
}
