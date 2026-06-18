//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Keukhan
//  Copyright (c) 2026 OvenMediaLabs. All rights reserved.
//
//==============================================================================
#include "decoder_avcodec_video.h"

#include "../../transcoder_private.h"
#include "base/info/application.h"

bool AVCodecVideoDecoder::Initialize()
{
	// Create the bitstream framer bound to this decoder's codec (once).
	if (_framer.IsValid() == false)
	{
		if (_framer.Init(GetCodecID()) == false)
		{
			logte("Bitstream parser not found");
			return false;
		}
	}

	const char *decoder_name = nullptr;
	switch (GetCodecID())
	{
		case cmn::MediaCodecId::H264:
			decoder_name = "h264";
			break;
		case cmn::MediaCodecId::H265:
			decoder_name = "hevc";
			break;
		case cmn::MediaCodecId::Vp8:
			decoder_name = "vp8";
			break;
		default:
			logte("Unsupported codec for video decoder: %s", cmn::GetCodecIdString(GetCodecID()));
			return false;
	}

	if (_codec.AllocDecoderByName(decoder_name) == false)
	{
		logte("Could not allocate decoder context for %s", cmn::GetCodecIdString(GetCodecID()));
		return false;
	}

	_codec.SetTimeBase(GetTimebase());
	_codec.SetThreadCount(GetRefTrack()->GetThreadCount());
	_codec.SetThreadTypeFrame();

	if (_codec.Open() == false)
	{
		logte("Could not open decoder(%s). %s", cmn::GetCodecIdString(GetCodecID()), _codec.GetLastErrorString().CStr());
		return false;
	}

	_change_format = false;

	return true;
}

bool AVCodecVideoDecoder::ReinitCodecIfNeed()
{
	if (_codec.GetWidth() != 0 &&
		_codec.GetHeight() != 0 &&
		(_framer.GetWidth() != _codec.GetWidth() || _framer.GetHeight() != _codec.GetHeight()))
	{
		logti("[%s] Input frame resolution of the %u track has been changed. Size:%dx%d -> %dx%d",
			  _stream_info.GetUri().CStr(), GetRefTrack()->GetId(),
			  _codec.GetWidth(), _codec.GetHeight(),
			  _framer.GetWidth(), _framer.GetHeight());

		Uninitialize();

		if (Initialize() == false)
		{
			return false;
		}
	}

	return true;
}

std::shared_ptr<MediaPacket> AVCodecVideoDecoder::GetFramedPacket()
{
	if (_framing_buffer.GetRemainedSize() <= 0)
	{
		auto obj = _input_buffer.Dequeue();
		if (obj.has_value() == false)
		{
			return nullptr;
		}

		auto media_packet = std::move(obj.value());
		if (_framing_buffer.Append(media_packet, media_packet->GetData()) == false)
		{
			logte("[%s] Could not prepare framing buffer", _stream_info.GetUri().CStr());
			_framing_buffer.Reset();
			return nullptr;
		}
	}

	auto *data = _framing_buffer.DataAtCurrentOffset();
	if (data == nullptr)
	{
		_framing_buffer.Reset();
		return nullptr;
	}

	int parsed_size = 0;
	auto parsed_pkt = _framer.Parse(
		_codec,
		cmn::MediaType::Video,
		data,
		_framing_buffer.GetRemainedSize(),
		_framing_buffer.GetPts(),
		_framing_buffer.GetDts(), parsed_size);
	if (parsed_size < 0)
	{
		logte("[%s] An error occurred while parsing: %d", _stream_info.GetUri().CStr(), parsed_size);
		_framing_buffer.Reset();
		return nullptr;
	}
	else if (parsed_size > 0)
	{
		_framing_buffer.Advance(parsed_size);
	}

	// No complete frame yet; more input data is needed.
	if (parsed_pkt == nullptr)
	{
		return nullptr;
	}

	return parsed_pkt;
}


DecodeResult AVCodecVideoDecoder::SendPacket(const std::shared_ptr<MediaPacket> &packet)
{
	if (ReinitCodecIfNeed() == false)
	{
		return DecodeResult::NoOutput();
	}

	bool drop_non_keyframe = (GetRefTrack()->IsKeyframeDecodeOnly() == true) && (packet->GetFlag() != MediaPacketFlag::Key);
	if (drop_non_keyframe == true)
	{
		return DecodeResult::NoOutput();
	}

	auto result = _codec.SendPacket(packet);
	if (result == ffmpeg::CodecResult::InvalidData)
	{
		logtd("[%s] Invalid data while sending a packet for decoding. track(%u), pts(%" PRId64 ")",
			  _stream_info.GetUri().CStr(), GetRefTrack()->GetId(), packet->GetPts());

		auto empty_frame = MediaFrame::Create(cmn::MediaType::Video, _framing_buffer.GetDts());

		return DecodeResult::InvalidData(std::move(empty_frame));
	}
	else if (result != ffmpeg::CodecResult::Ok)
	{
		logte("Error occurred while sending a packet for decoding. reason(%s)", _codec.GetLastErrorString().CStr());

		return DecodeResult::Error();
	}

	return DecodeResult::NoOutput();
}

DecodeResult AVCodecVideoDecoder::ReceiveFrame()
{
	auto received = _codec.ReceiveFrame();
	auto result = received.result;
	if (result == ffmpeg::CodecResult::Again)
	{
		return DecodeResult::NoOutput();
	}
	else if (result == ffmpeg::CodecResult::InvalidData)
	{
		logtw("[%s] Invalid data while receiving a frame for decoding.", _stream_info.GetUri().CStr());
		return DecodeResult::InvalidData();
	}
	else if (result != ffmpeg::CodecResult::Ok)
	{
		logte("Error receiving a packet for decoding. reason(%s)", _codec.GetLastErrorString().CStr());
		return DecodeResult::Error();
	}

	bool format_changed = (_change_format == false);
	if (format_changed == true)
	{
		auto codec_info = _codec.GetCodecInfoString();
		logtd("[%s(%u)] Changed format. %s", _stream_info.GetUri().CStr(), _stream_info.GetId(), codec_info.CStr());
		_change_format = true;
	}

	auto decoded_frame = received.frame;
	if (decoded_frame == nullptr)
	{
		return DecodeResult::NoOutput();
	}

	// If the decoder did not provide a duration, calculate it from the frame rate 
	if (decoded_frame->GetDuration() <= 0LL && _codec.GetFrameRate().num > 0 && _codec.GetFrameRate().den > 0)
	{
		decoded_frame->SetDuration((int64_t)(((double)_codec.GetFrameRate().den / (double)_codec.GetFrameRate().num) / (double)GetRefTrack()->GetTimeBase().GetExpr()));
	}

	return DecodeResult::Decoded(std::move(decoded_frame), format_changed);
}

void AVCodecVideoDecoder::Uninitialize()
{
	_codec.Flush();
	_codec.Reset();
}
