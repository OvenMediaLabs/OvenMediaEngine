//==============================================================================
//
//  Transcode
//
//  Copyright (c) 2026 AirenSoft. All rights reserved.
//
//==============================================================================
#include "encoder_avif.h"

#include <climits>

#include "../../transcoder_private.h"

namespace
{
	// Locates the OBU_SEQUENCE_HEADER unit (header bytes included) in a
	// low-overhead AV1 bitstream. libaom leaves codec-context extradata empty
	// even with GLOBAL_HEADER, and the avif muxer derives the av1C box from
	// extradata — so the in-band copy each key frame carries is the source.
	bool FindSequenceHeaderObu(const uint8_t *data, size_t size, size_t &offset, size_t &length)
	{
		size_t pos = 0;
		while (pos < size)
		{
			const size_t start = pos;
			const uint8_t header = data[pos++];
			if ((header & 0x80) != 0)
			{
				return false;
			}
			const int type = (header >> 3) & 0x0F;
			const bool has_extension = (header & 0x04) != 0;
			const bool has_size = (header & 0x02) != 0;
			if (has_extension)
			{
				if (pos >= size)
				{
					return false;
				}
				pos++;
			}
			if (has_size == false)
			{
				// A sizeless OBU is only valid as the last unit in the frame.
				if (type == 1)
				{
					offset = start;
					length = size - start;
					return true;
				}
				return false;
			}
			uint64_t obu_size = 0;
			int shift = 0;
			bool more = true;
			while (more)
			{
				// AV1 caps leb128 at 8 bytes.
				if (pos >= size || shift >= 56)
				{
					return false;
				}
				const uint8_t b = data[pos++];
				obu_size |= static_cast<uint64_t>(b & 0x7F) << shift;
				more = (b & 0x80) != 0;
				shift += 7;
			}
			if (obu_size > size - pos)
			{
				return false;
			}
			pos += obu_size;
			if (type == 1)	// OBU_SEQUENCE_HEADER
			{
				offset = start;
				length = pos - start;
				return true;
			}
		}
		return false;
	}
}  // namespace

EncoderAVIF::~EncoderAVIF()
{
	Uninitialize();
}

void EncoderAVIF::Uninitialize()
{
	if (_packet != nullptr)
	{
		::av_packet_free(&_packet);
	}
	if (_codec_params != nullptr)
	{
		::avcodec_parameters_free(&_codec_params);
	}
	if (_codec_context != nullptr)
	{
		::avcodec_free_context(&_codec_context);
	}
}

bool EncoderAVIF::SetCodecParams()
{
	_codec_context->codec_type = AVMEDIA_TYPE_VIDEO;
	_codec_context->framerate = ::av_d2q((GetRefTrack()->GetFrameRateByConfig() > 0) ? GetRefTrack()->GetFrameRateByConfig() : GetRefTrack()->GetFrameRateByMeasured(), AV_TIME_BASE);
	auto time_base = GetRefTrack()->GetTimeBase();
	_codec_context->time_base = AVRational{time_base.GetNum(), time_base.GetDen()};
	_codec_context->pix_fmt = ffmpeg::compat::ToAVPixelFormat(GetSupportVideoFormat());
	auto resolution = GetRefTrack()->GetResolution();
	_codec_context->width = resolution.width;
	_codec_context->height = resolution.height;

	// usage=allintra makes every frame an intra key frame; still_picture left off
	// (this encodes a sequence of stills).
	::av_opt_set(_codec_context->priv_data, "usage", "allintra", 0);
	_codec_context->gop_size = 0;

	// GLOBAL_HEADER is required: the avif muxer builds the av1C box from extradata.
	// Without it the box is empty and libavif (Chrome's AVIF decoder) rejects the
	// file, even though dav1d still decodes the mdat.
	_codec_context->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

	// Single-threaded: thumbnails at ~1fps gain nothing from threading.
	_codec_context->thread_count = 1;

	// cpu-used is libaom's speed/effort knob (0 slowest/best .. 8 fastest).
	// CreateOutputTrack has validated Speed into 0-8 (-1 = unset).
	int speed = GetRefTrack()->GetSpeed();
	::av_opt_set_int(_codec_context->priv_data, "cpu-used", (speed != -1) ? speed : 8, 0);

	// crf is libaom's constant-quality knob (0-63, lower = higher quality). <Crf>
	// arrives on the quality field validated into 0-63 with the default applied
	// (0 is a valid maximum-quality value); bit_rate=0 selects pure CQ (AOM_Q).
	int crf = GetRefTrack()->GetQuality();
	::av_opt_set_int(_codec_context->priv_data, "crf", crf, 0);
	_codec_context->bit_rate = 0;

	return true;
}

bool EncoderAVIF::Initialize()
{
	// libaom-av1 by name: the options above are libaom-specific, and it is the
	// only AV1 encoder enabled in this build.
	const AVCodec *codec = ::avcodec_find_encoder_by_name("libaom-av1");
	if (codec == nullptr)
	{
		logte("Could not find encoder: %s", cmn::GetCodecIdString(GetCodecID()));
		return false;
	}

	_codec_context = ::avcodec_alloc_context3(codec);
	if (_codec_context == nullptr)
	{
		logte("Could not allocate codec context for %s", cmn::GetCodecIdString(GetCodecID()));
		return false;
	}

	if (SetCodecParams() == false)
	{
		logte("Could not set codec parameters for %s", cmn::GetCodecIdString(GetCodecID()));
		return false;
	}

	_packet = ::av_packet_alloc();
	if (_packet == nullptr)
	{
		logte("Could not allocate packet for %s", cmn::GetCodecIdString(GetCodecID()));
		return false;
	}

	// avcodec_open2 is deferred to the first frame (OpenCodecWithFrameColor) so the
	// codec context can copy that frame's colour tags.
	return true;
}

bool EncoderAVIF::OpenCodecWithFrameColor(const AVFrame *frame)
{
	// The rescaler has already converted the samples to BT.709 full range and
	// applied the untagged-source policy, so the frame tags describe the pixels
	// exactly. Copying them makes the AV1 sequence header (and thus the AVIF CICP)
	// signal precisely what was encoded.
	_codec_context->color_primaries = frame->color_primaries;
	_codec_context->color_trc = frame->color_trc;
	_codec_context->colorspace = frame->colorspace;
	_codec_context->color_range = frame->color_range;

	// AVIF must signal a valid CICP; a still-unspecified field (an untagged
	// source the rescaler did not fully stamp) would make strict decoders guess.
	// Default to BT.709 full range, matching the rescaler's canonical target.
	if (_codec_context->color_primaries == AVCOL_PRI_UNSPECIFIED)
		_codec_context->color_primaries = AVCOL_PRI_BT709;
	if (_codec_context->color_trc == AVCOL_TRC_UNSPECIFIED)
		_codec_context->color_trc = AVCOL_TRC_BT709;
	if (_codec_context->colorspace == AVCOL_SPC_UNSPECIFIED)
		_codec_context->colorspace = AVCOL_SPC_BT709;
	if (_codec_context->color_range == AVCOL_RANGE_UNSPECIFIED)
		_codec_context->color_range = AVCOL_RANGE_JPEG;

	if (::avcodec_open2(_codec_context, nullptr, nullptr) < 0)
	{
		logte("Could not open codec: %s", cmn::GetCodecIdString(GetCodecID()));
		return false;
	}

	// Snapshot the codec parameters once: the extradata (av1C) is fixed after open
	// and every muxed image reuses it.
	_codec_params = ::avcodec_parameters_alloc();
	if (_codec_params == nullptr ||
		::avcodec_parameters_from_context(_codec_params, _codec_context) < 0)
	{
		logte("Could not snapshot codec parameters for %s", cmn::GetCodecIdString(GetCodecID()));
		return false;
	}

	return true;
}

EncodeResult EncoderAVIF::SendFrame(const std::shared_ptr<const MediaFrame> &media_frame, bool force_keyframe)
{
	(void)force_keyframe;  // allintra: every frame is already a key frame.

	if (media_frame == nullptr)
	{
		// Flush.
		if (_codec_context != nullptr && _codec_params != nullptr)
		{
			::avcodec_send_frame(_codec_context, nullptr);
		}
		return EncodeResult::NoOutput();
	}

	// A dead codec keeps consuming input so the upstream never stalls.
	if (_codec_dead)
	{
		return EncodeResult::NoOutput();
	}

	auto *frame = static_cast<AVFrame *>(media_frame->GetPrivData());
	if (frame == nullptr)
	{
		return EncodeResult::Error();
	}

	if (_codec_params == nullptr)
	{
		if (OpenCodecWithFrameColor(frame) == false)
		{
			_codec_dead = true;
			return EncodeResult::Error();
		}
	}

	int ret = ::avcodec_send_frame(_codec_context, frame);
	if (ret < 0 && ret != AVERROR(EAGAIN))
	{
		logte("Error sending a frame to the AVIF encoder: %s", ffmpeg::compat::AVErrorToString(ret).CStr());
		return EncodeResult::Error();
	}

	return EncodeResult::NoOutput();
}

EncodeResult EncoderAVIF::ReceivePacket()
{
	if (_codec_context == nullptr || _codec_params == nullptr)
	{
		return EncodeResult::NoOutput();
	}

	int ret = ::avcodec_receive_packet(_codec_context, _packet);
	if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
	{
		return EncodeResult::NoOutput();
	}
	if (ret < 0)
	{
		logte("Error receiving an AV1 packet for AVIF: %s", ffmpeg::compat::AVErrorToString(ret).CStr());
		return EncodeResult::Error();
	}

	// libaom leaves codec-context extradata empty, so the av1C box is built from
	// the in-band sequence header each key frame carries.
	if (_codec_params->extradata_size == 0)
	{
		size_t obu_offset = 0;
		size_t obu_length = 0;
		if (FindSequenceHeaderObu(_packet->data, _packet->size, obu_offset, obu_length))
		{
			auto *extradata = static_cast<uint8_t *>(::av_mallocz(obu_length + AV_INPUT_BUFFER_PADDING_SIZE));
			if (extradata != nullptr)
			{
				::memcpy(extradata, _packet->data + obu_offset, obu_length);
				_codec_params->extradata = extradata;
				_codec_params->extradata_size = static_cast<int>(obu_length);
			}
		}
		if (_codec_params->extradata_size == 0)
		{
			logtw("Could not extract the AV1 sequence header for av1C; dropping this thumbnail frame");
			::av_packet_unref(_packet);
			return EncodeResult::NoOutput();
		}
	}

	std::vector<uint8_t> avif_bytes;
	bool muxed = MuxAvif(_codec_params, _codec_context->time_base, _packet->data, _packet->size, avif_bytes);

	int64_t pts = _packet->pts;
	int64_t dts = _packet->dts;
	int64_t duration = _packet->duration;
	::av_packet_unref(_packet);

	if (muxed == false)
	{
		logtw("Failed to mux the AVIF thumbnail; dropping this frame");
		return EncodeResult::NoOutput();
	}

	auto media_packet = std::make_shared<MediaPacket>(
		0,
		cmn::MediaType::Video,
		0,
		avif_bytes.data(),
		static_cast<int32_t>(avif_bytes.size()),
		pts, dts, duration,
		MediaPacketFlag::Key,
		cmn::BitstreamFormat::AVIF,
		cmn::PacketType::RAW);

	return EncodeResult::Encoded(std::move(media_packet));
}

bool EncoderAVIF::MuxAvif(const AVCodecParameters *codec_params, AVRational time_base, const void *data, size_t size, std::vector<uint8_t> &out)
{
	out.clear();

	// The packet API takes int sizes; reject anything that would not fit.
	if (size > static_cast<size_t>(INT_MAX))
	{
		logte("AVIF input of %zu bytes exceeds the muxer's size limit", size);
		return false;
	}

	AVFormatContext *fmt_ctx = nullptr;
	if (::avformat_alloc_output_context2(&fmt_ctx, nullptr, "avif", nullptr) < 0 || fmt_ctx == nullptr)
	{
		logte("Could not allocate AVIF muxer context");
		return false;
	}

	// In-memory, seekable output: the avif muxer back-patches HEIF box sizes, which
	// the dynamic buffer supports; avio_close_dyn_buf returns the finished bytes and
	// frees the internal buffer.
	if (::avio_open_dyn_buf(&fmt_ctx->pb) < 0)
	{
		::avformat_free_context(fmt_ctx);
		return false;
	}

	bool ok = true;
	AVStream *stream = ::avformat_new_stream(fmt_ctx, nullptr);
	if (stream == nullptr || ::avcodec_parameters_copy(stream->codecpar, codec_params) < 0)
	{
		ok = false;
	}
	else
	{
		stream->time_base = time_base;

		if (::avformat_write_header(fmt_ctx, nullptr) < 0)
		{
			logte("Could not write AVIF header");
			ok = false;
		}
		else
		{
			AVPacket *pkt = ::av_packet_alloc();
			if (pkt == nullptr || ::av_new_packet(pkt, static_cast<int>(size)) < 0)
			{
				ok = false;
			}
			else
			{
				::memcpy(pkt->data, data, size);
				pkt->stream_index = stream->index;
				pkt->pts = 0;
				pkt->dts = 0;
				pkt->flags |= AV_PKT_FLAG_KEY;
				if (::av_interleaved_write_frame(fmt_ctx, pkt) < 0)
				{
					logte("Could not write AVIF frame");
					ok = false;
				}
			}
			::av_packet_free(&pkt);

			if (::av_write_trailer(fmt_ctx) < 0)
			{
				logte("Could not write AVIF trailer");
				ok = false;
			}
		}
	}

	// Always close the dynamic buffer to recover and free it, even on error.
	uint8_t *buf = nullptr;
	int buf_size = ::avio_close_dyn_buf(fmt_ctx->pb, &buf);
	fmt_ctx->pb = nullptr;
	if (ok && buf_size > 0 && buf != nullptr)
	{
		out.assign(buf, buf + buf_size);
	}
	::av_free(buf);
	::avformat_free_context(fmt_ctx);

	return ok && !out.empty();
}
