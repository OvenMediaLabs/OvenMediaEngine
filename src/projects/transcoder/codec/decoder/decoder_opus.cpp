//==============================================================================
//
//  Transcode
//
//  Created by Kwon Keuk Han
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================
#include "decoder_opus.h"

#include "../../transcoder_private.h"
#include "base/info/application.h"

bool DecoderOPUS::InitCodec()
{
	const AVCodec *codec = ::avcodec_find_decoder(ffmpeg::compat::ToAVCodecId(GetCodecID()));
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

	_codec_context->time_base = ffmpeg::compat::TimebaseToAVRational(GetTimebase());

	if (::avcodec_open2(_codec_context, nullptr, nullptr) < 0)
	{
		logte("Could not open codec: %s", cmn::GetCodecIdString(GetCodecID()));
		return false;
	}

	_parser = ::av_parser_init(ffmpeg::compat::ToAVCodecId(GetCodecID()));
	if (_parser == nullptr)
	{
		logte("Parser not found");
		return false;
	}
	
	_parser->flags |= PARSER_FLAG_COMPLETE_FRAMES;

	_change_format = false;
	
	return true;
}

void DecoderOPUS::CodecThread()
{
	ov::logger::ThreadHelper thread_helper;

	// Initialize the codec and notify the main thread.
	if(_codec_init_event.Submit(InitCodec()) == false)
	{
		return;
	}
	
	bool no_data_to_encode = false;

	while (!_kill_flag)
	{
		/////////////////////////////////////////////////////////////////////
		// Sending a packet to decoder
		/////////////////////////////////////////////////////////////////////
		if (_cur_pkt == nullptr && (_input_buffer.IsEmpty() == false || no_data_to_encode == true))
		{
			auto obj = _input_buffer.Dequeue();
			if (obj.has_value() == false)
			{
				continue;
			}

			no_data_to_encode = false;
			_cur_pkt = std::move(obj.value());
			if (_cur_pkt != nullptr)
			{
				_cur_data = _cur_pkt->GetData();
				_pkt_offset = 0;
			}

			if ((_cur_data == nullptr) || (_cur_data->GetLength() == 0))
			{
				continue;
			}
		}

		if (_cur_data != nullptr)
		{
			if (_pkt_offset < _cur_data->GetLength())
			{
				_pkt->size = 0;

				int32_t parsed_size = ::av_parser_parse2(
					_parser,
					_codec_context,
					&_pkt->data, &_pkt->size,
					_cur_data->GetDataAs<uint8_t>() + _pkt_offset,
					static_cast<int32_t>(_cur_data->GetLength() - _pkt_offset),
					_cur_pkt->GetPts(), _cur_pkt->GetPts(),
					0);

				// Failed to parsing
				if (parsed_size <= 0)
				{
					logte("Error while parsing\n");
					_cur_data = nullptr;
				}
				else
				{
					OV_ASSERT(_cur_data->GetLength() >= (size_t)parsed_size, "Current data size MUST greater than parsed_size, but data size: %ld, parsed_size: %ld", _cur_data->GetLength(), parsed_size);
					_pkt_offset += parsed_size;
				}

				if (_pkt->size > 0)
				{
					_pkt->pts = _parser->pts;
					_pkt->dts = _parser->dts;
					_pkt->flags = (_parser->key_frame == 1) ? AV_PKT_FLAG_KEY : 0;
					if (_pkt->pts != AV_NOPTS_VALUE && _parser->last_pts != AV_NOPTS_VALUE)
					{
						_pkt->duration = _pkt->pts - _parser->last_pts;
					}
					else
					{
						_pkt->duration = 0;
					}

					int ret = ::avcodec_send_packet(_codec_context, _pkt);
					if (ret == AVERROR(EAGAIN))
					{
						// Nothing to do here, just continue
					}
					else if (ret == AVERROR_INVALIDDATA)
					{
						logtd("[%s] Invalid data while sending a packet for decoding. track(%u), pts(%lld)",
							  _stream_info.GetUri().CStr(), GetRefTrack()->GetId(), _pkt->pts);
					}
					else if (ret < 0)
					{
						_cur_data = nullptr;
						logte("Error occurred while sending a packet for decoding. reason(%s)", ffmpeg::compat::AVErrorToString(ret).CStr());

						Complete(TranscodeResult::DataError, nullptr);
					}

					// Save first pakcet's PTS
					if(_first_pkt_pts == INT64_MIN)
					{
						_first_pkt_pts = _pkt->pts;
					}						
				}
			}

			if (_cur_data == nullptr || _cur_data->GetLength() <= _pkt_offset)
			{
				_cur_pkt = nullptr;
				_cur_data = nullptr;
				_pkt_offset = 0;
			}
		}

		/////////////////////////////////////////////////////////////////////
		// Receive a frame from decoder
		/////////////////////////////////////////////////////////////////////
		// Check the decoded frame is available
		int ret = ::avcodec_receive_frame(_codec_context, _frame);
		if (ret == AVERROR(EAGAIN))
		{
			no_data_to_encode = true;
			continue;
		}
		else if (ret == AVERROR_INVALIDDATA)
		{
			logtw("Invalid data while receiving a packet for decoding");
			continue;
		}
		else if (ret < 0)
		{
			logte("Error receiving a packet for decoding. reason(%s)", ffmpeg::compat::AVErrorToString(ret).CStr());

			Complete(TranscodeResult::DataError, nullptr);

			continue;
		}
		else
		{
			// Update codec informations if needed
			if (_change_format == false)
			{
				auto codec_info = ffmpeg::compat::CodecInfoToString(_codec_context);

				logtd("[%s(%u)] Changed format. %s", _stream_info.GetUri().CStr(), _stream_info.GetId(), codec_info.CStr());
			}

			// The actual duration is calculated based on the number of samples in the decoded frame.
			_frame->pkt_duration = ffmpeg::compat::GetDurationPerFrame(cmn::MediaType::Audio, GetRefTrack(), _frame);

			// If the decoded audio frame has no PTS, add the frame duration to the previous frame's PTS.
			if (_frame->pts == AV_NOPTS_VALUE)
			{
				if(_last_pkt_pts == INT64_MIN)
				{
					// If the previous frame has no PTS, use the PTS of the first packet.
					_frame->pts = _first_pkt_pts;
				}
				else 
				{
					_frame->pts = _last_pkt_pts + _last_pkt_duration;
				}
			}

			auto output_frame = ffmpeg::compat::ToMediaFrame(cmn::MediaType::Audio, _frame);
			::av_frame_unref(_frame);
			if (output_frame == nullptr)
			{
				continue;
			}

			_last_pkt_pts = output_frame->GetPts();
			_last_pkt_duration = output_frame->GetDuration();

			Complete(!_change_format ? TranscodeResult::FormatChanged : TranscodeResult::DataReady, std::move(output_frame));
			_change_format = true;
		}
	}
}
