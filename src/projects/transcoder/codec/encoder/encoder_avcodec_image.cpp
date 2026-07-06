//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Keukhan
//  Copyright (c) 2026 OvenMediaLabs. All rights reserved.
//
//==============================================================================
#include "encoder_avcodec_image.h"

#include <climits>
#include <unistd.h>

#include "../../transcoder_private.h"

extern "C"
{
#include <libavformat/avformat.h>
}

namespace
{
	// Locates the OBU_SEQUENCE_HEADER unit (header bytes included) in a low-overhead
	// AV1 bitstream. libaom leaves codec-context extradata empty even with
	// GLOBAL_HEADER, and the avif muxer derives the av1C box from extradata -- so the
	// in-band copy each key frame carries is the source.
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

void AVCodecImageEncoder::SetParamsCommon()
{
	_codec.SetMediaType(cmn::MediaType::Video);
	_codec.SetFrameRate(cmn::Rational::FromDouble((GetRefTrack()->GetFrameRateByConfig() > 0) ? GetRefTrack()->GetFrameRateByConfig() : GetRefTrack()->GetFrameRateByMeasured()));
	_codec.SetTimeBase(GetRefTrack()->GetTimeBase());
	_codec.SetPixelFormat(GetSupportVideoFormat());
	auto resolution = GetRefTrack()->GetResolution();
	_codec.SetWidth(resolution.width);
	_codec.SetHeight(resolution.height);

	_bitstream_format = GetBitstreamFormat();
	_packet_type = cmn::PacketType::RAW;
}

bool AVCodecImageEncoder::SetParamsJpeg()
{
	SetParamsCommon();

	_codec.SetFixedQScale();
	_codec.SetGlobalQualityFromQp(_codec.GetQMin());

	// Set color range to JPEG
	_codec.SetColorRange(cmn::ColorRange::Full);
	_codec.SetStrictCompliance();

	return true;
}

bool AVCodecImageEncoder::SetParamsPng()
{
	SetParamsCommon();

	_codec.SetCompressionLevel(1);

	return true;
}

bool AVCodecImageEncoder::SetParamsWebp()
{
	SetParamsCommon();

	_codec.SetCompressionLevel(1);

	auto preset = GetRefTrack()->GetPreset();
	if (preset.IsEmpty())
	{
		_codec.SetOption("preset", "default");
	}
	else if (preset == "none" || preset == "default" || preset == "picture" ||
			 preset == "photo" || preset == "drawing" || preset == "icon" || preset == "text")
	{
		_codec.SetOption("preset", preset.CStr());
	}

	return true;
}

bool AVCodecImageEncoder::SetParamsAvif()
{
	SetParamsCommon();

	// allintra makes every frame an intra key frame (a sequence of stills); gop 0.
	_codec.SetOption("usage", "allintra");
	_codec.SetGopSize(0);

	// GLOBAL_HEADER is required: the avif muxer builds the av1C box from extradata.
	// Without it the box is empty and libavif (Chrome's AVIF decoder) rejects the
	// file even though dav1d still decodes the mdat.
	_codec.Get()->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

	// Thumbnails at ~1fps gain nothing from threading.
	_codec.SetThreadCount(1);

	// cpu-used is libaom's speed/effort knob (0 slowest/best .. 8 fastest); crf is
	// its constant-quality knob (0-63, lower = higher quality). bit_rate 0 selects
	// pure constant quality (AOM_Q).
	_codec.SetOption("cpu-used", static_cast<int64_t>(8));
	_codec.SetOption("crf", static_cast<int64_t>(23));
	_codec.SetBitrate(0);

	return true;
}

bool AVCodecImageEncoder::OpenCodec()
{
	if (_codec.AllocEncoder(GetCodecID()) == false)
	{
		logte("Could not allocate encoder context for %s", cmn::GetCodecIdString(GetCodecID()));
		return false;
	}

	bool result = false;
	switch (_codec_id)
	{
		case cmn::MediaCodecId::Jpeg:
			result = SetParamsJpeg();
			break;
		case cmn::MediaCodecId::Png:
			result = SetParamsPng();
			break;
		case cmn::MediaCodecId::Avif:
			result = SetParamsAvif();
			break;
		case cmn::MediaCodecId::Webp:
		default:
			result = SetParamsWebp();
			break;
	}

	if (result == false)
	{
		logte("Could not set codec parameters for %s", cmn::GetCodecIdString(GetCodecID()));
		return false;
	}

	// AVIF defers avcodec_open2 to the first frame (OpenAvifCodecWithFrameColor) so
	// the AV1 CICP can copy that frame's colour tags. JPEG/PNG/WEBP open now.
	if (_codec_id == cmn::MediaCodecId::Avif)
	{
		_avif_packet = ::av_packet_alloc();
		if (_avif_packet == nullptr)
		{
			logte("Could not allocate packet for %s", cmn::GetCodecIdString(GetCodecID()));
			return false;
		}
		return true;
	}

	if (_codec.Open() == false)
	{
		logte("Could not open encoder(%s). %s", cmn::GetCodecIdString(GetCodecID()), _codec.GetLastErrorString().CStr());
		return false;
	}

	return true;
}

bool AVCodecImageEncoder::Initialize()
{
	auto result = OpenCodec();
	if (_track != nullptr)
	{
		_track->SetCodecStatus(result ? cmn::CodecStatus::Ready : cmn::CodecStatus::Failed);
	}

	return result;
}

void AVCodecImageEncoder::Uninitialize()
{
	// AVIF defers open; capture whether it actually opened before tearing down.
	bool avif_opened = (_avif_codec_params != nullptr);

	if (_avif_packet != nullptr)
	{
		::av_packet_free(&_avif_packet);
	}
	if (_avif_codec_params != nullptr)
	{
		::avcodec_parameters_free(&_avif_codec_params);
	}

	// Only an opened codec can be flushed; an AVIF track that never received a
	// frame was never opened.
	if (_codec_id != cmn::MediaCodecId::Avif || avif_opened)
	{
		_codec.Flush();
	}
	_codec.Reset();
}

EncodeResult AVCodecImageEncoder::SendFrame(const std::shared_ptr<const MediaFrame> &frame, bool force_keyframe)
{
	if (_codec_id == cmn::MediaCodecId::Avif)
	{
		return SendFrameAvif(frame);
	}

	// Flush the encoder if the frame is nullptr.
	if (frame == nullptr)
	{
		if (_codec.SendFrame(nullptr) != ffmpeg::CodecResult::Ok)
		{
			logte("Error sending a frame for encoding. reason(%s)", _codec.GetLastErrorString().CStr());
		}
		return EncodeResult::NoOutput();
	}

	auto result = _codec.SendFrame(frame, force_keyframe);
	if (result == ffmpeg::CodecResult::Again)
	{
		logtw("Encoder internal buffer is full, need to flush packets.");
	}
	else if (result == ffmpeg::CodecResult::InvalidData)
	{
		logtw("Invalid data while sending a frame for encoding.");
	}
	else if (result == ffmpeg::CodecResult::NoMemory)
	{
		logte("Could not allocate memory while sending a frame for encoding.");
		return EncodeResult::Error();
	}
	else if (result != ffmpeg::CodecResult::Ok)
	{
		logte("Error sending a frame for encoding. reason(%s)", _codec.GetLastErrorString().CStr());
		return EncodeResult::Error();
	}

	return EncodeResult::NoOutput();
}

EncodeResult AVCodecImageEncoder::ReceivePacket()
{
	if (_codec_id == cmn::MediaCodecId::Avif)
	{
		return ReceivePacketAvif();
	}

	auto [result, media_packet] = _codec.ReceivePacket(_bitstream_format, _packet_type);
	if (result == ffmpeg::CodecResult::Again || result == ffmpeg::CodecResult::Eof)
	{
		return EncodeResult::NoOutput();
	}
	else if (result == ffmpeg::CodecResult::InvalidData)
	{
		logtw("Invalid data while receiving a packet for encoding.");
		return EncodeResult::NoOutput();
	}
	else if (result == ffmpeg::CodecResult::NoMemory)
	{
		logtw("Could not allocate memory while receiving a packet for encoding.");
		return EncodeResult::Error();
	}
	else if (result != ffmpeg::CodecResult::Ok)
	{
		logte("Error receiving a packet for encoding : %s", _codec.GetLastErrorString().CStr());
		return EncodeResult::Error();
	}

	if (media_packet == nullptr)
	{
		logte("Could not allocate the media packet");
		return EncodeResult::Error();
	}

	if (GetRefTrack()->GetMediaType() == cmn::MediaType::Audio)
	{
		// If the pts value is under zero, the dash packetizer does not work. Drop it but keep draining.
		if (media_packet->GetPts() < 0)
		{
			return EncodeResult::NoOutput();
		}
	}

#if DEBUG
	if (GetRefTrack()->GetMediaType() == cmn::MediaType::Video && media_packet->IsKeyFrame() == true)
	{
		logtt("keyframe is encoded. pts:%" PRId64 "ms, dts:%" PRId64 "ms, delta:%" PRId64 "ms",
			  static_cast<int64_t>(media_packet->GetPts() * 1000 / GetRefTrack()->GetTimeBase().GetTimescale()),
			  static_cast<int64_t>(media_packet->GetDts() * 1000 / GetRefTrack()->GetTimeBase().GetTimescale()),
			  static_cast<int64_t>(_last_keyframe_delta_time * 1000 / GetRefTrack()->GetTimeBase().GetTimescale()));
	}
#endif

	return EncodeResult::Encoded(std::move(media_packet));
}

EncodeResult AVCodecImageEncoder::SendFrameAvif(const std::shared_ptr<const MediaFrame> &media_frame)
{
	// Flush: only an opened codec can be drained.
	if (media_frame == nullptr)
	{
		if (_avif_codec_params != nullptr)
		{
			_codec.SendFrame(nullptr);
		}
		return EncodeResult::NoOutput();
	}

	// A dead codec keeps consuming input so the upstream never stalls.
	if (_avif_codec_dead)
	{
		return EncodeResult::NoOutput();
	}

	auto *frame = static_cast<AVFrame *>(media_frame->GetPrivData());
	if (frame == nullptr)
	{
		return EncodeResult::Error();
	}

	// Deferred open on the first frame so the AV1 CICP copies its colour tags. On
	// failure, mark the codec dead and drop frames (NoOutput) rather than erroring
	// out the encode thread: a failed thumbnail must not stall the stream.
	if (_avif_codec_params == nullptr)
	{
		if (OpenAvifCodecWithFrameColor(frame) == false)
		{
			_avif_codec_dead = true;
			return EncodeResult::NoOutput();
		}
	}

	auto result = _codec.SendFrame(media_frame, false);
	if (result != ffmpeg::CodecResult::Ok && result != ffmpeg::CodecResult::Again)
	{
		logte("Error sending a frame to the AVIF encoder. reason(%s)", _codec.GetLastErrorString().CStr());
		return EncodeResult::Error();
	}

	return EncodeResult::NoOutput();
}

bool AVCodecImageEncoder::OpenAvifCodecWithFrameColor(const AVFrame *frame)
{
	// The rescaler already converted to BT.709 (full range) and applied the
	// untagged-source policy, so the frame tags describe the pixels exactly. Copying
	// them makes the AV1 sequence header (and thus the AVIF CICP) signal precisely
	// what was encoded.
	AVCodecContext *context = _codec.Get();
	context->color_primaries = frame->color_primaries;
	context->color_trc		 = frame->color_trc;
	context->colorspace		 = frame->colorspace;
	context->color_range	 = frame->color_range;

	// AVIF must signal a valid CICP; default any still-unspecified field to BT.709
	// full range, matching the rescaler's canonical target.
	if (context->color_primaries == AVCOL_PRI_UNSPECIFIED)
		context->color_primaries = AVCOL_PRI_BT709;
	if (context->color_trc == AVCOL_TRC_UNSPECIFIED)
		context->color_trc = AVCOL_TRC_BT709;
	if (context->colorspace == AVCOL_SPC_UNSPECIFIED)
		context->colorspace = AVCOL_SPC_BT709;
	if (context->color_range == AVCOL_RANGE_UNSPECIFIED)
		context->color_range = AVCOL_RANGE_JPEG;

	if (_codec.Open() == false)
	{
		logte("Could not open the AVIF (libaom-av1) encoder. %s", _codec.GetLastErrorString().CStr());
		return false;
	}

	// Snapshot codec parameters once: the av1C extradata is fixed after open and
	// every muxed image reuses it.
	_avif_codec_params = ::avcodec_parameters_alloc();
	if (_avif_codec_params == nullptr ||
		::avcodec_parameters_from_context(_avif_codec_params, context) < 0)
	{
		logte("Could not snapshot AVIF codec parameters for %s", cmn::GetCodecIdString(GetCodecID()));
		return false;
	}

	return true;
}

EncodeResult AVCodecImageEncoder::ReceivePacketAvif()
{
	// No frame sent yet -> codec not opened.
	if (_avif_codec_params == nullptr)
	{
		return EncodeResult::NoOutput();
	}

	int ret = ::avcodec_receive_packet(_codec.Get(), _avif_packet);
	if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
	{
		return EncodeResult::NoOutput();
	}
	if (ret < 0)
	{
		logte("Error receiving an AV1 packet for AVIF: %s", ffmpeg::compat::AVErrorToString(ret).CStr());
		return EncodeResult::Error();
	}

	// libaom leaves codec-context extradata empty, so the av1C box is built from the
	// in-band sequence header each key frame carries.
	if (_avif_codec_params->extradata_size == 0)
	{
		size_t obu_offset = 0;
		size_t obu_length = 0;
		if (FindSequenceHeaderObu(_avif_packet->data, _avif_packet->size, obu_offset, obu_length))
		{
			auto *extradata = static_cast<uint8_t *>(::av_mallocz(obu_length + AV_INPUT_BUFFER_PADDING_SIZE));
			if (extradata != nullptr)
			{
				::memcpy(extradata, _avif_packet->data + obu_offset, obu_length);
				_avif_codec_params->extradata = extradata;
				_avif_codec_params->extradata_size = static_cast<int>(obu_length);
			}
		}
		if (_avif_codec_params->extradata_size == 0)
		{
			logtw("Could not extract the AV1 sequence header for av1C; dropping this thumbnail frame");
			::av_packet_unref(_avif_packet);
			return EncodeResult::NoOutput();
		}
	}

	std::vector<uint8_t> avif_bytes;
	bool muxed = MuxAvif(_avif_codec_params, _codec.Get()->time_base, _avif_packet->data, _avif_packet->size, avif_bytes);

	int64_t pts = _avif_packet->pts;
	int64_t dts = _avif_packet->dts;
	int64_t duration = _avif_packet->duration;
	::av_packet_unref(_avif_packet);

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

bool AVCodecImageEncoder::MuxAvif(const AVCodecParameters *codec_params, AVRational time_base, const void *data, size_t size, std::vector<uint8_t> &out)
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
