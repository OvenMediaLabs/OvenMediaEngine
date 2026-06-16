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

#include <cstring>

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavutil/channel_layout.h>
#include <libavutil/mem.h>
#include <libavutil/opt.h>
}

namespace ffmpeg
{
	// Result of a codec send/receive operation, decoupled from FFmpeg's AVERROR codes.
	// Again/Eof are normal flow-control signals, not errors.
	enum class CodecResult
	{
		Ok,			 // Succeeded (packet accepted / frame produced)
		Again,		 // AVERROR(EAGAIN): need more input, or no output ready yet
		Eof,		 // AVERROR_EOF: fully flushed
		InvalidData, // AVERROR_INVALIDDATA: corrupt input
		NoMemory,	 // AVERROR(ENOMEM): allocation failure
		Error,		 // Any other failure (see GetLastErrorString())
	};

	// Output of ReceiveFrame(): the decode result plus the produced MediaFrame (non-null only
	// when result == CodecResult::Ok).
	struct ReceiveResult
	{
		CodecResult result;
		std::shared_ptr<MediaFrame> frame;
	};

	// Output of ReceivePacket(): the encode result plus the produced MediaPacket (non-null only
	// when result == CodecResult::Ok).
	struct ReceivePacketResult
	{
		CodecResult result;
		std::shared_ptr<MediaPacket> packet;
	};

	class FFmpegCodec
	{
	public:
		FFmpegCodec() = default;

		~FFmpegCodec();

		bool AllocDecoder(cmn::MediaCodecId codec_id);

		bool AllocDecoderByName(const char *decoder_name);

		bool AllocEncoder(cmn::MediaCodecId codec_id);

		bool AllocEncoderByName(const char *encoder_name);

		bool Open();

		CodecResult SendPacket(const std::shared_ptr<MediaPacket> &media_packet);

		ReceiveResult ReceiveFrame();

		CodecResult SendFrame(const std::shared_ptr<const MediaFrame> &media_frame, bool force_keyframe = false);

		ReceivePacketResult ReceivePacket(cmn::BitstreamFormat bitstream_format, cmn::PacketType packet_type);

		// Human-readable description of the most recent SendPacket/ReceiveFrame error.
		ov::String GetLastErrorString() const;

		// Human-readable codec/format summary (codec, resolution/sample rate, bitrate, ...).
		ov::String GetCodecInfoString() const;

		void SetMediaType(cmn::MediaType media_type);
		void SetTimeBase(const cmn::Timebase &time_base);
		void SetPacketTimeBase(const cmn::Timebase &pkt_timebase);
		void SetFrameRate(const cmn::Rational &framerate);
		void SetSampleAspectRatio(const cmn::Rational &sar);
		void SetWidth(int width);
		void SetHeight(int height);
		void SetPixelFormat(cmn::VideoPixelFormatId pixel_format);
		void SetProfile(int profile);
		void SetLevel(int level);
		void SetBitrate(int64_t bit_rate);
		void SetRcMinRate(int64_t rc_min_rate);
		void SetRcMaxRate(int64_t rc_max_rate);
		void SetRcBufferSize(int rc_buffer_size);
		void SetGopSize(int gop_size);
		void SetMaxBFrames(int max_b_frames);
		void SetTicksPerFrame(int ticks_per_frame);
		void SetThreadCount(int thread_count);
		void SetThreadTypeFrame();
		void SetSlices(int slices);
		void SetQMin(int qmin);
		void SetQMax(int qmax);
		// Sets the fixed quality from a quantization parameter (QP), converted to FFmpeg's internal
		// lambda scale. Takes effect only together with SetFixedQScale().
		void SetGlobalQualityFromQp(int qp);
		void SetColorRange(cmn::ColorRange color_range);
		// Requires the encoder to strictly follow the standard (no experimental/non-compliant features).
		void SetStrictCompliance();
		void SetCompressionLevel(int compression_level);
		void SetInitialPadding(int initial_padding);
		void SetSampleFormat(cmn::AudioSample::Format sample_fmt);
		void SetSampleRate(int sample_rate);
		// Uses a fixed quantizer scale instead of rate control (the quantizer comes from
		// SetGlobalQualityFromQp()). Typically used for image encoders such as MJPEG.
		void SetFixedQScale();
		void SetLowDelay();
		void SetDefaultChannelLayout(int channels);
		void SetExtradata(const uint8_t *data, int size);
		bool SetHwDeviceContext(const std::shared_ptr<HwDeviceContext> &device_ctx);
		void UnrefHwDeviceContext();
		// Allocates and attaches a hardware frames context derived from the codec's hw_device_ctx.
		// Requires SetHwDeviceContext() to have been called first.
		bool SetHwFramesContext();
		int SetOption(const char *name, const char *value);
		int SetOption(const char *name, int64_t value);

		int64_t GetBitrate() const noexcept;
		AVRational GetFrameRate() const noexcept;
		int GetWidth() const noexcept;
		int GetHeight() const noexcept;
		int GetGopSize() const noexcept;
		int GetThreadCount() const noexcept;
		int GetQMin() const noexcept;
		int GetFrameSize() const noexcept;
		void *GetPrivData() const noexcept;

		AVCodecContext *Get() const noexcept;

		void Flush();

		void Reset();

	private:
		bool AllocInternal(const AVCodec *codec);

		// Maps an FFmpeg send/receive return code to CodecResult
		CodecResult ToCodecResult(int error);

		AVCodecContext *_context = nullptr;
		AVPacket *_send_packet = nullptr;	 // Reused input packet filled by SendPacket(MediaPacket).
		AVFrame *_receive_frame = nullptr;	 // Reused output frame used by ReceiveFrame().
		AVPacket *_receive_packet = nullptr; // Reused output packet used by ReceivePacket().
		int _last_error = 0;
	};
}
