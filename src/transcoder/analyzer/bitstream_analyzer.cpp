//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Keukhan
//  Copyright (c) 2026 OvenMediaLabs. All rights reserved.
//
//==============================================================================

#include "bitstream_analyzer.h"

#include <modules/bitstream/aac/aac_adts.h>
#include <modules/bitstream/av1/av1_parser.h>
#include <modules/bitstream/av1/av1_types.h>
#include <modules/bitstream/h264/h264_parser.h>
#include <modules/bitstream/h265/h265_parser.h>
#include <modules/bitstream/h265/h265_types.h>
#include <modules/bitstream/mp3/mp3_parser.h>
#include <modules/bitstream/nalu/nal_unit_bitstream_parser.h>
#include <modules/bitstream/opus/opus_parser.h>
#include <modules/bitstream/vp8/vp8_parser.h>

#define OV_LOG_TAG "BitstreamAnalyzer"

bool BitstreamAnalyzer::Init(cmn::MediaCodecId codec_id)
{
	Close();

	switch (codec_id)
	{
		// Video: analyzed for resolution.
		case cmn::MediaCodecId::H264:
		case cmn::MediaCodecId::H265:
		case cmn::MediaCodecId::Av1:
		case cmn::MediaCodecId::Vp8:
		// Audio: analyzed for sample rate / channels + multi-frame detection.
		case cmn::MediaCodecId::Aac:
		case cmn::MediaCodecId::Mp3:
		case cmn::MediaCodecId::Mp2:
		case cmn::MediaCodecId::Opus:
			_codec_id = codec_id;
			_valid	  = true;
			return true;
		default:
			return false;
	}
}

bool BitstreamAnalyzer::Analyze(const std::shared_ptr<const MediaPacket> &packet)
{
	if (_valid == false || packet == nullptr)
	{
		return false;
	}

	const auto &data = packet->GetData();
	if (data == nullptr || data->GetLength() == 0)
	{
		return false;
	}

	const cmn::MediaType media_type = packet->GetMediaType();
	const uint8_t *buf				= data->GetDataAs<uint8_t>();
	const int buf_size				= static_cast<int>(data->GetLength());

	_pts							= packet->GetPts();
	_dts							= packet->GetDts();

	// One pass extracts the format (resolution or sample rate/channels) and counts
	// the frames at the same time.
	if (media_type == cmn::MediaType::Video)
	{
		_frame_count = AnalyzeVideo(buf, buf_size);
	}
	else if (media_type == cmn::MediaType::Audio)
	{
		_frame_count = AnalyzeAudio(buf, buf_size);
	}
	else
	{
		_frame_count = 1;
	}

	// One frame per packet is expected; warn once if there are more.
	if (_frame_count > 1 && _multi_frame_warned == false)
	{
		logtw("%s packet has %d frames but only one is expected (pts=%" PRId64 ")", 
			cmn::GetMediaTypeString(media_type), _frame_count, _pts);
		_multi_frame_warned = true;
	}

	return true;
}

bool BitstreamAnalyzer::IsFormatChanged(const std::shared_ptr<const MediaPacket> &packet)
{
	// Remember the format before this packet is analyzed.
	const int prev_width	   = _width;
	const int prev_height	   = _height;
	const int prev_sample_rate = _sample_rate;
	const int prev_channels	   = _channels;

	if (Analyze(packet) == false)
	{
		return false;
	}

	// All-zero means this is the first analysis (or right after Close()); just set
	// the baseline and report "no change".
	if (prev_width == 0 && prev_height == 0 && prev_sample_rate == 0 && prev_channels == 0)
	{
		return false;
	}

	return _width != prev_width ||
		   _height != prev_height ||
		   _sample_rate != prev_sample_rate ||
		   _channels != prev_channels;
}

ov::String BitstreamAnalyzer::GetInfoString() const
{
	ov::String info;
	info.AppendFormat("codec=%s", cmn::GetCodecIdString(_codec_id));

	if (_width > 0 || _height > 0)
	{
		info.AppendFormat(", resolution=%dx%d", _width, _height);
	}
	if (_sample_rate > 0 || _channels > 0)
	{
		info.AppendFormat(", samplerate=%d, channels=%d", _sample_rate, _channels);
	}

	info.AppendFormat(", pts=%" PRId64 ", dts=%" PRId64 ", frames=%d", _pts, _dts, _frame_count);

	return info;
}

int BitstreamAnalyzer::AnalyzeVideo(const uint8_t *buf, int buf_size)
{
	switch (_codec_id)
	{
		// H.264 / H.265 must be Annex-B (start-code NAL units); MediaRouter has
		// already converted it. AVCC / HVCC input would find no resolution.
		//
		// Resolution comes from the SPS; each picture starts with a slice whose
		// first_mb_in_slice == 0 (H.264) / first_slice_segment_in_pic_flag == 1
		// (H.265). One NAL-unit scan (offsets only, no copy) does both.
		case cmn::MediaCodecId::H264: {
			int count = 0;
			for (const auto &idx : H264Parser::FindNaluIndexes(buf, buf_size))
			{
				const uint8_t *nalu = buf + idx._payload_offset;
				size_t len			= idx._payload_size;

				NalUnitBitstreamParser parser(nalu, len);
				H264NalUnitHeader header;
				if (H264Parser::ParseNalUnitHeader(parser, header) == false)
				{
					continue;
				}

				if (header.GetNalUnitType() == H264NalUnitType::Sps)
				{
					H264SPS sps;
					if (H264Parser::ParseSPS(nalu, len, sps) == true)
					{
						_width	= static_cast<int>(sps.GetWidth());
						_height = static_cast<int>(sps.GetHeight());
					}
				}
				else if (header.IsVideoSlice() == true)
				{
					uint32_t first_mb_in_slice = 0;
					if (parser.ReadUEV(first_mb_in_slice) == true && first_mb_in_slice == 0)
					{
						count++;
					}
				}
			}
			return count > 0 ? count : 1;
		}
		case cmn::MediaCodecId::H265: {
			int count = 0;
			for (const auto &idx : H264Parser::FindNaluIndexes(buf, buf_size))
			{
				const uint8_t *nalu = buf + idx._payload_offset;
				size_t len			= idx._payload_size;

				H265NalUnitHeader header;
				if (H265Parser::ParseNalUnitHeader(nalu, len, header) == false)
				{
					continue;
				}

				const auto nal_type = header.GetNalUnitType();
				if (nal_type == H265NALUnitType::SPS)
				{
					H265SPS sps;
					if (H265Parser::ParseSPS(nalu, len, sps) == true)
					{
						_width	= static_cast<int>(sps.GetWidth());
						_height = static_cast<int>(sps.GetHeight());
					}
					continue;
				}

				// Slice NAL types are 0..31; headers (VPS/SPS/PPS/...) are 32+.
				if (static_cast<uint8_t>(nal_type) > 31)
				{
					continue;
				}

				NalUnitBitstreamParser parser(nalu, len);
				uint8_t first_slice_segment_in_pic_flag = 0;
				if (parser.Skip(16) == true &&
					parser.ReadBit(first_slice_segment_in_pic_flag) == true &&
					first_slice_segment_in_pic_flag == 1)
				{
					count++;
				}
			}
			return count > 0 ? count : 1;
		}
		case cmn::MediaCodecId::Av1: {
			// Resolution comes from the sequence header; each frame is an OBU_FRAME or
			// a standalone OBU_FRAME_HEADER. One OBU walk does both.
			int count	  = 0;
			size_t offset = 0;
			while (offset < static_cast<size_t>(buf_size))
			{
				Av1ObuSpan span;
				if (Av1Parser::ReadObu(buf, buf_size, offset, span) == false)
				{
					break;
				}

				if (span.header.type == Av1ObuType::SequenceHeader)
				{
					auto summary = Av1Parser::ParseSequenceHeaderSummary(buf + span.payload_offset, span.payload_size);
					if (summary.has_value() && summary->parsed == true)
					{
						_width	= static_cast<int>(summary->max_frame_width);
						_height = static_cast<int>(summary->max_frame_height);
					}
				}
				else if (span.header.type == Av1ObuType::Frame ||
						 span.header.type == Av1ObuType::FrameHeader)
				{
					count++;
				}

				if (span.next_offset <= offset)
				{
					break;	// not moving forward; avoid an endless loop
				}
				offset = span.next_offset;
			}
			return count > 0 ? count : 1;
		}
		case cmn::MediaCodecId::Vp8: {
			// VP8 has the resolution only in key frames; otherwise keep the last value.
			// A VP8 payload is always a single frame.
			VP8Parser parser;
			if (VP8Parser::Parse(buf, buf_size, parser) == true && parser.GetWidth() > 0)
			{
				_width	= static_cast<int>(parser.GetWidth());
				_height = static_cast<int>(parser.GetHeight());
			}
			return 1;
		}
		default:
			return 1;
	}
}

int BitstreamAnalyzer::AnalyzeAudio(const uint8_t *buf, int buf_size)
{
	switch (_codec_id)
	{
		// Walk the self-framed headers (ADTS-AAC, MP3/MP2), reading the format from
		// the first frame and counting all frames in one pass.
		case cmn::MediaCodecId::Aac: {
			int offset = 0;
			int count  = 0;
			while (offset < buf_size)
			{
				// Only ADTS has a frame length; Parse rejects raw AAC (no syncword).
				AACAdts adts;
				if (AACAdts::Parse(buf + offset, buf_size - offset, adts) == false)
				{
					break;
				}

				if (count == 0)
				{
					_sample_rate = static_cast<int>(adts.Samplerate());

					// channel_config is the channel count for 1..6; 7 means 8 channels.
					const uint8_t channel_config = adts.ChannelConfiguration();
					_channels					  = (channel_config == 7) ? 8 : static_cast<int>(channel_config);
				}

				const int frame_len = static_cast<int>(adts.AacFrameLength());
				if (frame_len <= 0 || offset + frame_len > buf_size)
				{
					// Unknown length or a leftover partial frame; stop counting.
					break;
				}

				count++;
				offset += frame_len;
			}
			return count > 0 ? count : 1;
		}
		case cmn::MediaCodecId::Mp3:
		case cmn::MediaCodecId::Mp2: {
			int offset = 0;
			int count  = 0;
			while (offset < buf_size)
			{
				MP3Parser parser;
				if (MP3Parser::Parse(buf + offset, buf_size - offset, parser) == false)
				{
					break;
				}

				if (count == 0)
				{
					_sample_rate = static_cast<int>(parser.GetSampleRate());
					_channels	 = static_cast<int>(parser.GetChannelCount());
				}

				const int frame_len = static_cast<int>(parser.GetFrameLength());
				if (frame_len <= 0 || offset + frame_len > buf_size)
				{
					break;
				}

				count++;
				offset += frame_len;
			}
			return count > 0 ? count : 1;
		}
		case cmn::MediaCodecId::Opus: {
			// Opus always decodes at 48 kHz; the TOC byte carries the stereo flag.
			// One packet, no in-band framing.
			OPUSParser parser;
			if (OPUSParser::Parse(buf, buf_size, parser) == true)
			{
				_sample_rate = 48000;
				_channels	 = (parser.GetStereoFlag() != 0) ? 2 : 1;
			}
			return 1;
		}
		default:
			return 1;
	}
}

void BitstreamAnalyzer::Close()
{
	_codec_id			= cmn::MediaCodecId::None;
	_valid				= false;
	_width				= 0;
	_height				= 0;
	_sample_rate		= 0;
	_channels			= 0;
	_pts				= 0;
	_dts				= 0;
	_frame_count		= 0;
	_multi_frame_warned = false;
}
