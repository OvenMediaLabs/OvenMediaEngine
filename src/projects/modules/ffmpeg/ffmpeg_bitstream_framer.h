//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Keukhan
//  Copyright (c) 2026 OvenMediaLabs. All rights reserved.
//
//==============================================================================

#pragma once

#include <base/ovlibrary/ovlibrary.h>
#include <modules/ffmpeg/compat.h>
#include <modules/ffmpeg/ffmpeg_codec.h>

#include <memory>

extern "C"
{
#include <libavcodec/avcodec.h>
}

namespace ffmpeg
{
	class FFmpegBitstreamFramer
	{
	public:
		FFmpegBitstreamFramer() = default;

		~FFmpegBitstreamFramer();

		// PARSER_FLAG_COMPLETE_FRAMES - Require complete frames
		bool Init(cmn::MediaCodecId codec_id, int flags = PARSER_FLAG_COMPLETE_FRAMES);

		bool IsValid() const noexcept;

		std::shared_ptr<MediaPacket> Parse(const FFmpegCodec &codec_context, cmn::MediaType media_type,
										   const uint8_t *buf, int buf_size,
										   int64_t pts, int64_t dts, int &consumed, int64_t pos = 0);

		int64_t GetPts() const noexcept;
		int64_t GetDts() const noexcept;
		int64_t GetLastDts() const noexcept;
		bool IsKeyFrame() const noexcept;
		int GetWidth() const noexcept;
		int GetHeight() const noexcept;

		void Close();

	private:
		int Parse(const FFmpegCodec &codec_context, uint8_t **poutbuf, int *poutbuf_size,
				  const uint8_t *buf, int buf_size, int64_t pts, int64_t dts, int64_t pos = 0);

		AVCodecParserContext *_parser = nullptr;
	};
}  // namespace ffmpeg
