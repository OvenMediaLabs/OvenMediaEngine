//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Keukhan
//  Copyright (c) 2026 OvenMediaLabs. All rights reserved.
//
//==============================================================================
#pragma once

#include "../../transcoder_encoder.h"
#include <modules/ffmpeg/ffmpeg_codec.h>

// AVCodecVideoEncoder handles the software FFmpeg video encoders:
//   - H.264 : libx264 (X264) / libopenh264 (OPENH264, DEFAULT)
//   - VP8   : libvpx (LIBVPX)
class AVCodecVideoEncoder : public TranscodeEncoder
{
public:
	AVCodecVideoEncoder(const info::Stream &stream_info, cmn::MediaCodecId codec_id, cmn::MediaCodecModuleId module_id)
		: TranscodeEncoder(stream_info), _codec_id(codec_id), _module_id(module_id)
	{
	}

	~AVCodecVideoEncoder() override;

	// ----- Codec info -----
	cmn::MediaCodecId GetCodecID() const noexcept override { return _codec_id; }
	cmn::MediaCodecModuleId GetModuleID() const noexcept override { return _module_id; }
	cmn::MediaType GetMediaType() const noexcept override { return cmn::MediaType::Video; }
	bool IsHWAccel() const noexcept override { return false; }

	// ----- Supported formats -----
	cmn::AudioSample::Format GetSupportAudioFormat() const noexcept override { return cmn::AudioSample::Format::None; }
	cmn::VideoPixelFormatId GetSupportVideoFormat() const noexcept override { return cmn::VideoPixelFormatId::YUV420P; }
	cmn::BitstreamFormat GetBitstreamFormat() const noexcept override
	{
		switch (_codec_id)
		{
			case cmn::MediaCodecId::Vp8:
				return cmn::BitstreamFormat::VP8;
			case cmn::MediaCodecId::H264:
				return cmn::BitstreamFormat::H264_ANNEXB;
			case cmn::MediaCodecId::H265:
				return cmn::BitstreamFormat::H265_ANNEXB;
			default:
				return cmn::BitstreamFormat::Unknown;
		}
	}

protected:
	// ----- Encoder interface -----
	bool Initialize() override;
	void Uninitialize() override;
	EncodeResult SendFrame(const std::shared_ptr<const MediaFrame> &frame, bool force_keyframe) override;
	EncodeResult ReceivePacket() override;

private:
	// ----- Internal helpers -----
	bool OpenCodec();
	bool SetParamsX264();
	bool SetParamsOpenH264();
	bool SetParamsVp8();

	// ----- Members -----
	cmn::MediaCodecId _codec_id;
	cmn::MediaCodecModuleId _module_id;
	ffmpeg::FFmpegCodec _codec;
	cmn::BitstreamFormat _bitstream_format = cmn::BitstreamFormat::Unknown;
	cmn::PacketType _packet_type = cmn::PacketType::Unknown;

};
