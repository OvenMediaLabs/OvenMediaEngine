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
		uint8_t *out_data = nullptr;
		int out_size	  = 0;

		consumed		  = Parse(codec_context, &out_data, &out_size, buf, buf_size, pts, dts, pos);

		// The parser has not yet assembled a complete frame from the input.
		if (out_data == nullptr || out_size <= 0)
		{
			return nullptr;
		}

		return std::make_shared<MediaPacket>(
			0,
			media_type,
			0,
			out_data,
			out_size,
			GetPts(),
			GetDts(),
			GetDts() - GetLastDts(),
			IsKeyFrame() ? MediaPacketFlag::Key : MediaPacketFlag::NoFlag,
			cmn::BitstreamFormat::Unknown,
			cmn::PacketType::Unknown);
	}

	int64_t FFmpegBitstreamFramer::GetPts() const noexcept
	{
		return _parser->pts;
	}
	int64_t FFmpegBitstreamFramer::GetDts() const noexcept
	{
		return _parser->dts;
	}
	int64_t FFmpegBitstreamFramer::GetLastDts() const noexcept
	{
		return _parser->last_dts;
	}
	bool FFmpegBitstreamFramer::IsKeyFrame() const noexcept
	{
		return _parser->key_frame == 1;
	}
	int FFmpegBitstreamFramer::GetWidth() const noexcept
	{
		return _parser->width;
	}
	int FFmpegBitstreamFramer::GetHeight() const noexcept
	{
		return _parser->height;
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
