//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Keukhan
//  Copyright (c) 2026 OvenMediaLabs. All rights reserved.
//
//==============================================================================

#include "ffmpeg_codec.h"

namespace ffmpeg
{
	FFmpegCodec::~FFmpegCodec()
	{
		Reset();
		OV_SAFE_FUNC(_send_packet, nullptr, ::av_packet_free, &);
		OV_SAFE_FUNC(_receive_frame, nullptr, ::av_frame_free, &);
		OV_SAFE_FUNC(_receive_packet, nullptr, ::av_packet_free, &);
	}

	bool FFmpegCodec::AllocDecoder(cmn::MediaCodecId codec_id)
	{
		const AVCodec *codec = ::avcodec_find_decoder(compat::ToAVCodecId(codec_id));
		if (codec == nullptr)
		{
			return false;
		}

		return AllocInternal(codec);
	}

	bool FFmpegCodec::AllocDecoderByName(const char *decoder_name)
	{
		const AVCodec *codec = ::avcodec_find_decoder_by_name(decoder_name);
		if (codec == nullptr)
		{
			return false;
		}

		return AllocInternal(codec);
	}

	bool FFmpegCodec::AllocEncoder(cmn::MediaCodecId codec_id)
	{
		const AVCodec *codec = ::avcodec_find_encoder(compat::ToAVCodecId(codec_id));
		if (codec == nullptr)
		{
			return false;
		}

		return AllocInternal(codec);
	}

	bool FFmpegCodec::AllocEncoderByName(const char *encoder_name)
	{
		const AVCodec *codec = ::avcodec_find_encoder_by_name(encoder_name);
		if (codec == nullptr)
		{
			return false;
		}

		return AllocInternal(codec);
	}

	bool FFmpegCodec::Open()
	{
		AVDictionary **options = nullptr;
		int result = ::avcodec_open2(_context, nullptr, options);
		if (result < 0)
		{
			_last_error = result;
			return false;
		}

		return true;
	}

	CodecResult FFmpegCodec::SendPacket(const std::shared_ptr<MediaPacket> &media_packet)
	{
		if (_send_packet == nullptr)
		{
			_send_packet = ::av_packet_alloc();
			if (_send_packet == nullptr)
			{
				return CodecResult::NoMemory;
			}
		}

		// MediaPacket -> AVPacket
		compat::ToAVPacket(_send_packet, media_packet);

		return ToCodecResult(::avcodec_send_packet(_context, _send_packet));
	}

	ReceiveResult FFmpegCodec::ReceiveFrame()
	{
		if (_receive_frame == nullptr)
		{
			_receive_frame = ::av_frame_alloc();
			if (_receive_frame == nullptr)
			{
				return { CodecResult::NoMemory, nullptr };
			}
		}

		CodecResult result = ToCodecResult(::avcodec_receive_frame(_context, _receive_frame));
		if (result != CodecResult::Ok)
		{
			return { result, nullptr };
		}

		auto media_frame = compat::ToMediaFrame(compat::ToMediaType(_context->codec_type), _receive_frame);
		::av_frame_unref(_receive_frame);

		return { result, std::move(media_frame) };
	}

	CodecResult FFmpegCodec::SendFrame(const std::shared_ptr<const MediaFrame> &media_frame, bool force_keyframe)
	{
		if (media_frame == nullptr)
		{
			return ToCodecResult(::avcodec_send_frame(_context, nullptr));
		}

		cmn::MediaType media_type = compat::ToMediaType(_context->codec_type);

		AVFrame *frame = static_cast<AVFrame *>(media_frame->GetPrivData());
		if (frame == nullptr)
		{
			return CodecResult::NoMemory;
		}

		// Apply the force-keyframe decision made (FFmpeg-free) by the base loop.
		if (media_type == cmn::MediaType::Video)
		{
			frame->pict_type = force_keyframe ? AV_PICTURE_TYPE_I : AV_PICTURE_TYPE_NONE;
		}

		return ToCodecResult(::avcodec_send_frame(_context, frame));
	}

	ReceivePacketResult FFmpegCodec::ReceivePacket(cmn::BitstreamFormat bitstream_format, cmn::PacketType packet_type)
	{
		if (_receive_packet == nullptr)
		{
			_receive_packet = ::av_packet_alloc();
			if (_receive_packet == nullptr)
			{
				return { CodecResult::NoMemory, nullptr };
			}
		}

		CodecResult result = ToCodecResult(::avcodec_receive_packet(_context, _receive_packet));
		if (result != CodecResult::Ok)
		{
			return { result, nullptr };
		}

		auto media_packet = compat::ToMediaPacket(_receive_packet, compat::ToMediaType(_context->codec_type), bitstream_format, packet_type);
		::av_packet_unref(_receive_packet);

		return { result, std::move(media_packet) };
	}

	// Human-readable description of the most recent SendPacket/ReceiveFrame error.
	ov::String FFmpegCodec::GetLastErrorString() const
	{
		char buffer[AV_ERROR_MAX_STRING_SIZE]{};
		::av_strerror(_last_error, buffer, sizeof(buffer));
		return buffer;
	}

	// Human-readable codec/format summary (codec, resolution/sample rate, bitrate, ...).
	ov::String FFmpegCodec::GetCodecInfoString() const { return compat::CodecInfoToString(_context); }

	void FFmpegCodec::SetMediaType(cmn::MediaType media_type) { _context->codec_type = compat::ToAVMediaType(media_type); }
	void FFmpegCodec::SetTimeBase(const cmn::Timebase &time_base) { _context->time_base = AVRational{time_base.GetNum(), time_base.GetDen()}; }
	void FFmpegCodec::SetPacketTimeBase(const cmn::Timebase &pkt_timebase) { _context->pkt_timebase = AVRational{pkt_timebase.GetNum(), pkt_timebase.GetDen()}; }
	void FFmpegCodec::SetFrameRate(const cmn::Rational &framerate) { _context->framerate = AVRational{framerate.GetNum(), framerate.GetDen()}; }
	void FFmpegCodec::SetSampleAspectRatio(const cmn::Rational &sar) { _context->sample_aspect_ratio = AVRational{sar.GetNum(), sar.GetDen()}; }
	void FFmpegCodec::SetWidth(int width) { _context->width = width; }
	void FFmpegCodec::SetHeight(int height) { _context->height = height; }
	void FFmpegCodec::SetPixelFormat(cmn::VideoPixelFormatId pixel_format) { _context->pix_fmt = compat::ToAVPixelFormat(pixel_format); }
	void FFmpegCodec::SetProfile(int profile) { _context->profile = profile; }
	void FFmpegCodec::SetLevel(int level) { _context->level = level; }
	void FFmpegCodec::SetBitrate(int64_t bit_rate) { _context->bit_rate = bit_rate; }
	void FFmpegCodec::SetRcMinRate(int64_t rc_min_rate) { _context->rc_min_rate = rc_min_rate; }
	void FFmpegCodec::SetRcMaxRate(int64_t rc_max_rate) { _context->rc_max_rate = rc_max_rate; }
	void FFmpegCodec::SetRcBufferSize(int rc_buffer_size) { _context->rc_buffer_size = rc_buffer_size; }
	void FFmpegCodec::SetGopSize(int gop_size) { _context->gop_size = gop_size; }
	void FFmpegCodec::SetMaxBFrames(int max_b_frames) { _context->max_b_frames = max_b_frames; }
	void FFmpegCodec::SetTicksPerFrame(int ticks_per_frame) { _context->ticks_per_frame = ticks_per_frame; }
	void FFmpegCodec::SetThreadCount(int thread_count) { _context->thread_count = thread_count; }
	void FFmpegCodec::SetThreadTypeFrame() { _context->thread_type = FF_THREAD_FRAME; }
	void FFmpegCodec::SetSlices(int slices) { _context->slices = slices; }
	void FFmpegCodec::SetQMin(int qmin) { _context->qmin = qmin; }
	void FFmpegCodec::SetQMax(int qmax) { _context->qmax = qmax; }
	// Sets the fixed quality from a quantization parameter (QP), converted to FFmpeg's internal
	// lambda scale. Takes effect only together with SetFixedQScale().
	void FFmpegCodec::SetGlobalQualityFromQp(int qp) { _context->global_quality = qp * FF_QP2LAMBDA; }
	void FFmpegCodec::SetColorRange(cmn::ColorRange color_range) { _context->color_range = compat::ToAVColorRange(color_range); }
	// Requires the encoder to strictly follow the standard (no experimental/non-compliant features).
	void FFmpegCodec::SetStrictCompliance() { _context->strict_std_compliance = FF_COMPLIANCE_STRICT; }
	void FFmpegCodec::SetCompressionLevel(int compression_level) { _context->compression_level = compression_level; }
	void FFmpegCodec::SetInitialPadding(int initial_padding) { _context->initial_padding = initial_padding; }
	void FFmpegCodec::SetSampleFormat(cmn::AudioSample::Format sample_fmt) { _context->sample_fmt = static_cast<AVSampleFormat>(compat::ToAvSampleFormat(sample_fmt)); }
	void FFmpegCodec::SetSampleRate(int sample_rate) { _context->sample_rate = sample_rate; }
	// Uses a fixed quantizer scale instead of rate control (the quantizer comes from
	// SetGlobalQualityFromQp()). Typically used for image encoders such as MJPEG.
	void FFmpegCodec::SetFixedQScale() { _context->flags |= AV_CODEC_FLAG_QSCALE; }
	void FFmpegCodec::SetLowDelay() { _context->flags |= AV_CODEC_FLAG_LOW_DELAY; }
	void FFmpegCodec::SetDefaultChannelLayout(int channels)
	{
		::av_channel_layout_default(&_context->ch_layout, channels);
	}
	void FFmpegCodec::SetExtradata(const uint8_t *data, int size)
	{
		_context->extradata_size = size;
		_context->extradata = static_cast<uint8_t *>(::av_malloc(size + AV_INPUT_BUFFER_PADDING_SIZE));
		std::memset(_context->extradata, 0, size + AV_INPUT_BUFFER_PADDING_SIZE);
		std::memcpy(_context->extradata, data, size);
	}
	bool FFmpegCodec::SetHwDeviceContext(const std::shared_ptr<HwDeviceContext> &device_ctx)
	{
		if (device_ctx == nullptr)
		{
			return false;
		}

		_context->hw_device_ctx = ::av_buffer_ref(static_cast<AVBufferRef *>(device_ctx->GetNativeHandle()));
		return _context->hw_device_ctx != nullptr;
	}
	void FFmpegCodec::UnrefHwDeviceContext()
	{
		if (_context->hw_device_ctx != nullptr)
		{
			::av_buffer_unref(&_context->hw_device_ctx);
		}
	}
	// Allocates and attaches a hardware frames context derived from the codec's hw_device_ctx.
	// Requires SetHwDeviceContext() to have been called first.
	bool FFmpegCodec::SetHwFramesContext()
	{
		if (_context->hw_device_ctx == nullptr)
		{
			return false;
		}

		AVBufferRef *hw_frames_ref = ::av_hwframe_ctx_alloc(_context->hw_device_ctx);
		if (hw_frames_ref == nullptr)
		{
			return false;
		}

		auto constraints = ::av_hwdevice_get_hwframe_constraints(_context->hw_device_ctx, nullptr);
		if (constraints == nullptr)
		{
			::av_buffer_unref(&hw_frames_ref);
			return false;
		}

		auto frames_ctx			   = reinterpret_cast<AVHWFramesContext *>(hw_frames_ref->data);
		frames_ctx->format		   = *(constraints->valid_hw_formats);
		frames_ctx->sw_format	   = *(constraints->valid_sw_formats);
		frames_ctx->width		   = _context->width;
		frames_ctx->height		   = _context->height;
		frames_ctx->initial_pool_size = 2;

		::av_hwframe_constraints_free(&constraints);

		if (::av_hwframe_ctx_init(hw_frames_ref) < 0)
		{
			::av_buffer_unref(&hw_frames_ref);
			return false;
		}

		_context->hw_frames_ctx = hw_frames_ref;

		return true;
	}
	int FFmpegCodec::SetOption(const char *name, const char *value) { return ::av_opt_set(_context->priv_data, name, value, 0); }
	int FFmpegCodec::SetOption(const char *name, int64_t value) { return ::av_opt_set_int(_context->priv_data, name, value, 0); }

	int64_t FFmpegCodec::GetBitrate() const noexcept { return _context->bit_rate; }
	AVRational FFmpegCodec::GetFrameRate() const noexcept { return _context->framerate; }
	int FFmpegCodec::GetWidth() const noexcept { return _context->width; }
	int FFmpegCodec::GetHeight() const noexcept { return _context->height; }
	int FFmpegCodec::GetGopSize() const noexcept { return _context->gop_size; }
	int FFmpegCodec::GetThreadCount() const noexcept { return _context->thread_count; }
	int FFmpegCodec::GetQMin() const noexcept { return _context->qmin; }
	int FFmpegCodec::GetFrameSize() const noexcept { return _context->frame_size; }
	void *FFmpegCodec::GetPrivData() const noexcept { return _context->priv_data; }

	AVCodecContext *FFmpegCodec::Get() const noexcept
	{
		return _context;
	}

	void FFmpegCodec::Flush()
	{
		if (_context == nullptr || _context->codec == nullptr)
		{
			return;
		}

		if (::av_codec_is_encoder(_context->codec) &&
			(_context->codec->capabilities & AV_CODEC_CAP_ENCODER_FLUSH) == 0)
		{
			return;
		}

		::avcodec_flush_buffers(_context);
	}

	void FFmpegCodec::Reset()
	{
		OV_SAFE_FUNC(_context, nullptr, ::avcodec_free_context, &);
	}

	// Allocates a fresh codec context for the given codec. Used internally by the Alloc*() helpers.
	bool FFmpegCodec::AllocInternal(const AVCodec *codec)
	{
		Reset();
		_context = ::avcodec_alloc_context3(codec);

		return _context != nullptr;
	}

	// Maps an FFmpeg send/receive return code to CodecResult, remembering the raw error for
	// GetLastErrorString().
	CodecResult FFmpegCodec::ToCodecResult(int error)
	{
		_last_error = error;

		if (error == 0)
		{
			return CodecResult::Ok;
		}
		if (error == AVERROR(EAGAIN))
		{
			return CodecResult::Again;
		}
		if (error == AVERROR_EOF)
		{
			return CodecResult::Eof;
		}
		if (error == AVERROR_INVALIDDATA)
		{
			return CodecResult::InvalidData;
		}
		if (error == AVERROR(ENOMEM))
		{
			return CodecResult::NoMemory;
		}

		return CodecResult::Error;
	}
}
