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
		case cmn::MediaCodecId::H264:
			decoder_name = "h264";
			break;
		case cmn::MediaCodecId::H265:
			decoder_name = "hevc";
			break;
		case cmn::MediaCodecId::Vp8:
			decoder_name = "vp8";
			break;
		case cmn::MediaCodecId::Av1:
			decoder_name = "libdav1d";
			break;
		default:
			logte("Unsupported codec for video decoder: %s", cmn::GetCodecIdString(GetCodecID()));
			return false;
	}

	if (_codec.AllocDecoderByName(decoder_name) == false)
	{
		// Fallback: if the preferred decoder is not available, try an alternative decoder.
		// For AV1, if libdav1d is not available, fall back to libaom-av1.
		// Note: This fallback mechanism is temporary and should be removed once libdav1d is guaranteed to be available.
		if ((GetCodecID() == cmn::MediaCodecId::Av1) && (_codec.AllocDecoderByName("libaom-av1") == true))
		{
			logtw("libdav1d decoder not found, falling back to libaom-av1. Rerun the prerequisites to rebuild FFmpeg with dav1d");
			decoder_name = "libaom-av1";
		}
		else
		{
			logte("Could not allocate decoder context for %s", cmn::GetCodecIdString(GetCodecID()));
			return false;
		}
	}

	_codec.SetTimeBase(GetTimebase());
	_codec.SetThreadCount(GetRefTrack()->GetThreadCount());
	_codec.SetThreadTypeFrame();

	// dav1d-only: keeps latency from growing as the thread count increases.
	// Keyframe-only decoding uses 1 so each keyframe comes out without waiting for the next
	if (::strcmp(decoder_name, "libdav1d") == 0)
	{
		_codec.SetOption("max_frame_delay", static_cast<int64_t>(GetRefTrack()->IsKeyframeDecodeOnly() ? 1 : 2));
	}

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
		_codec.GetHeight() != 0)
	{
		logti("[%s] Input frame resolution of the %u track has been changed. Size:%dx%d -> %dx%d",
			  _stream_info.GetUri().CStr(), GetRefTrack()->GetId(),
			  _codec.GetWidth(), _codec.GetHeight(),
			  _analyzer.GetWidth(), _analyzer.GetHeight());

		// Drain the frames still delayed inside the codec session and hand them
		// downstream before teardown, so the resolution change loses no frame.
		// This is the last chance to recover them, so a corrupt frame is skipped
		// instead of aborting (bounded against a codec stuck on invalid data)
		if (::avcodec_send_packet(_codec.Get(), nullptr) == 0)
		{
			for (int attempt = 0; attempt < 64; attempt++)
			{
				auto received = ReceiveFrame();
				if (received.result == TranscodeResult::DataReady || received.result == TranscodeResult::FormatChanged)
				{
					Complete(received.result, std::move(received.frame));
					continue;
				}
				if (received.result == TranscodeResult::NoData)
				{
					continue;
				}
				break;
			}
		}

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
	auto obj = _input_buffer.Dequeue();
	if (obj.has_value() == false)
	{
		return nullptr;
	}

	auto media_packet = std::move(obj.value());

	const auto &data = media_packet->GetData();
	if (data == nullptr || data->GetLength() == 0)
	{
		logte("[%s] Could not analyze the bitstream packet", _stream_info.GetUri().CStr());
		return nullptr;
	}

	// Analyze the packet; if the input format (resolution) changed, reinit the codec.
	if (_analyzer.IsFormatChanged(media_packet) == true)
	{
		if (ReinitCodecIfNeed() == false)
		{
			return nullptr;
		}
	}

	return std::const_pointer_cast<MediaPacket>(media_packet);
}


DecodeResult AVCodecVideoDecoder::SendPacket(const std::shared_ptr<MediaPacket> &packet)
{
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

		auto empty_frame = MediaFrame::Create(cmn::MediaType::Video, packet->GetDts());

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
	if (result == ffmpeg::CodecResult::Again || result == ffmpeg::CodecResult::Eof)
	{
		// Eof only appears while draining for a reinit; the session reopens right after
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
	const auto framerate = _codec.GetFrameRate();
	const auto timebase_expr = GetRefTrack()->GetTimeBase().GetExpr();
	if (decoded_frame->GetDuration() <= 0LL && framerate.GetExpr() > 0 && timebase_expr > 0)
	{
		decoded_frame->SetDuration((int64_t)((1.0 / framerate.GetExpr()) / timebase_expr));
	}

	return DecodeResult::Decoded(std::move(decoded_frame), format_changed);
}

void AVCodecVideoDecoder::Uninitialize()
{
	_codec.Flush();
	_codec.Reset();
}
