//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Keukhan
//  Copyright (c) 2026 OvenMediaLabs. All rights reserved.
//
//==============================================================================

#include "ffmpeg_bitstream_framer.h"

namespace ffmpeg
{
	FFmpegBitstreamFramer::~FFmpegBitstreamFramer()
	{
		Close();
	}

	bool FFmpegBitstreamFramer::Init(cmn::MediaCodecId codec_id, int flags)
	{
		Close();

		_parser = ::av_parser_init(compat::ToAVCodecId(codec_id));
		if (_parser == nullptr)
		{
			return false;
		}

		_parser->flags |= flags;

		return true;
	}

	bool FFmpegBitstreamFramer::IsValid() const noexcept
	{
		return _parser != nullptr;
	}

	std::shared_ptr<MediaPacket> FFmpegBitstreamFramer::Parse(const FFmpegCodec &codec_context, cmn::MediaType media_type,
													 const uint8_t *buf, int buf_size,
													 int64_t pts, int64_t dts, int &consumed, int64_t pos)
	{
		consumed		  = 0;

		if (IsValid() == false)
		{
			return nullptr;
		}

		uint8_t *out_data = nullptr;
		int out_size	  = 0;

		const int ret	  = Parse(codec_context, &out_data, &out_size, buf, buf_size, pts, dts, pos);
		if (ret < 0)
		{
			return nullptr;
		}

		consumed = ret;

		// The parser has not yet assembled a complete frame from the input.
		if (out_data == nullptr || out_size <= 0)
		{
			return nullptr;
		}

		// The first frame (or a frame without a valid DTS) has no reference to compute the duration from.
		const int64_t dts_value		 = GetDts();
		const int64_t last_dts_value = GetLastDts();
		const int64_t delta			 = (dts_value != AV_NOPTS_VALUE && last_dts_value != AV_NOPTS_VALUE)
										   ? (dts_value - last_dts_value)
										   : 0;
		const int64_t duration		 = (delta > 0) ? delta : 0;

		return std::make_shared<MediaPacket>(
			0,
			media_type,
			0,
			out_data,
			out_size,
			GetPts(),
			GetDts(),
			duration,
			IsKeyFrame() ? MediaPacketFlag::Key : MediaPacketFlag::NoFlag,
			cmn::BitstreamFormat::Unknown,
			cmn::PacketType::Unknown);
	}

	int64_t FFmpegBitstreamFramer::GetPts() const noexcept
	{
		return IsValid() ? _parser->pts : AV_NOPTS_VALUE;
	}
	int64_t FFmpegBitstreamFramer::GetDts() const noexcept
	{
		return IsValid() ? _parser->dts : AV_NOPTS_VALUE;
	}
	int64_t FFmpegBitstreamFramer::GetLastDts() const noexcept
	{
		return IsValid() ? _parser->last_dts : AV_NOPTS_VALUE;
	}
	bool FFmpegBitstreamFramer::IsKeyFrame() const noexcept
	{
		return IsValid() ? (_parser->key_frame == 1) : false;
	}
	int FFmpegBitstreamFramer::GetWidth() const noexcept
	{
		return IsValid() ? _parser->width : 0;
	}
	int FFmpegBitstreamFramer::GetHeight() const noexcept
	{
		return IsValid() ? _parser->height : 0;
	}

	void FFmpegBitstreamFramer::Close()
	{
		OV_SAFE_FUNC(_parser, nullptr, ::av_parser_close, );
	}

	int FFmpegBitstreamFramer::Parse(const FFmpegCodec &codec_context, uint8_t **poutbuf, int *poutbuf_size,
							const uint8_t *buf, int buf_size, int64_t pts, int64_t dts, int64_t pos)
	{
		return ::av_parser_parse2(_parser, codec_context.Get(), poutbuf, poutbuf_size, buf, buf_size, pts, dts, pos);
	}

}  // namespace ffmpeg
