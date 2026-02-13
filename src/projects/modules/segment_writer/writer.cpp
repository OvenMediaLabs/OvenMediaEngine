//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2020 AirenSoft. All rights reserved.
//
//==============================================================================
#include "writer.h"

extern "C"
{
#include <libavutil/avassert.h>
#include <libavutil/opt.h>
#include <libavutil/timestamp.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

#include <modules/bitstream/aac/aac_converter.h>
#include <modules/bitstream/nalu/nal_stream_converter.h>
#include <modules/ffmpeg/compat.h>

#define OV_LOG_TAG "Writer"

#define OV_LOG_PREFIX_FORMAT "[%p] "
#define OV_LOG_PREFIX_VALUE this

static AVRational RationalFromTimebase(const cmn::Timebase &timebase)
{
	return {timebase.GetNum(), timebase.GetDen()};
}

static ov::String StringFromTs(int64_t timestamp)
{
	ov::String str = (timestamp == AV_NOPTS_VALUE) ? "NOPTS" : ov::String::FormatString("%" PRId64, timestamp);
	return str;
}

static ov::String StringFromTime(int64_t timestamp, const AVRational *time_base)
{
	ov::String str = (timestamp == AV_NOPTS_VALUE) ? "NOPTS" : ov::String::FormatString("%.6g", ::av_q2d(*time_base) * timestamp);
	return str;
}

static ov::String StringFromError(int code)
{
	char str[AV_TS_MAX_STRING_SIZE]{0};
	::av_strerror(code, str, AV_TS_MAX_STRING_SIZE);
	return str;
}

Writer::Writer(Type type, MediaType media_type)
	: _type(type),
	  _media_type(media_type)
{
}

Writer::~Writer()
{
	OV_ASSERT((_data_stream == nullptr), "It seems to Writer is not finalized");
}

std::shared_ptr<AVFormatContext> Writer::CreateFormatContext(Type type)
{
	const char *format = nullptr;

	switch (type)
	{
		case Type::MpegTs:
			format = "mpegts";
			break;

		case Type::M4s:
			format = "mp4";
			break;
	}

	if (format == nullptr)
	{
		// Unknown type
		return nullptr;
	}

	AVFormatContext *format_context = nullptr;

	::avformat_alloc_output_context2(&format_context, nullptr, format, nullptr);

	if (format_context == nullptr)
	{
		logae("Could not create format context");
		return nullptr;
	}

	return std::shared_ptr<AVFormatContext>(format_context, [](AVFormatContext *format_context) {
		if (format_context != nullptr)
		{
			::avformat_free_context(format_context);
		}
	});
}

std::shared_ptr<AVIOContext> Writer::CreateAvioContext()
{
	auto buffer = static_cast<unsigned char *>(::av_malloc(32768));
	auto avio_context = ::avio_alloc_context(buffer, 32768, 1, this, nullptr, OnWrite, OnSeek);

	return std::shared_ptr<AVIOContext>(avio_context, [](AVIOContext *avio_context) {
		if (avio_context != nullptr)
		{
			OV_SAFE_FUNC(avio_context->buffer, nullptr, ::av_free, );
			::avio_context_free(&avio_context);
		}
	});
}

void Writer::LogPacket(const AVPacket *packet) const
{
	// Retains _format_context
	auto format_context = _format_context;
	const AVRational *time_base = &(format_context->streams[packet->stream_index]->time_base);

	logai("pts:%s pts_time:%s dts:%s dts_time:%s duration:%s duration_time:%s stream_index:%d",
		  StringFromTs(packet->pts).CStr(), StringFromTime(packet->pts, time_base).CStr(),
		  StringFromTs(packet->dts).CStr(), StringFromTime(packet->dts, time_base).CStr(),
		  StringFromTs(packet->duration).CStr(), StringFromTime(packet->duration, time_base).CStr(),
		  packet->stream_index);
}

#define WRITER_CASE(cond, state) \
	case cond:                   \
		return state;

static AVCodecID AvCodecIdFromMediaCodecId(cmn::MediaCodecId codec_id)
{
	switch (codec_id)
	{
		WRITER_CASE(cmn::MediaCodecId::None, AV_CODEC_ID_NONE)
		WRITER_CASE(cmn::MediaCodecId::H264, AV_CODEC_ID_H264)
		WRITER_CASE(cmn::MediaCodecId::H265, AV_CODEC_ID_H265)
		WRITER_CASE(cmn::MediaCodecId::Vp8, AV_CODEC_ID_VP8)
		WRITER_CASE(cmn::MediaCodecId::Vp9, AV_CODEC_ID_VP9)
		WRITER_CASE(cmn::MediaCodecId::Av1, AV_CODEC_ID_AV1)
		WRITER_CASE(cmn::MediaCodecId::Flv, AV_CODEC_ID_FLV1)
		WRITER_CASE(cmn::MediaCodecId::Aac, AV_CODEC_ID_AAC)
		WRITER_CASE(cmn::MediaCodecId::Mp3, AV_CODEC_ID_MP3)
		WRITER_CASE(cmn::MediaCodecId::Opus, AV_CODEC_ID_OPUS)
		WRITER_CASE(cmn::MediaCodecId::Jpeg, AV_CODEC_ID_JPEG2000)
		WRITER_CASE(cmn::MediaCodecId::Png, AV_CODEC_ID_PNG)
		WRITER_CASE(cmn::MediaCodecId::Webp, AV_CODEC_ID_WEBP)
		WRITER_CASE(cmn::MediaCodecId::Whisper, AV_CODEC_ID_NONE)
		WRITER_CASE(cmn::MediaCodecId::WebVTT, AV_CODEC_ID_NONE)
	}

	return AV_CODEC_ID_NONE;
}

static int AvChannelLayoutFromAudioChannelLayout(const cmn::AudioChannel::Layout &layout)
{
	switch (layout)
	{
		WRITER_CASE(cmn::AudioChannel::Layout::LayoutUnknown, 0)
		WRITER_CASE(cmn::AudioChannel::Layout::LayoutMono, AV_CH_LAYOUT_MONO)
		WRITER_CASE(cmn::AudioChannel::Layout::LayoutStereo, AV_CH_LAYOUT_STEREO)
		WRITER_CASE(cmn::AudioChannel::Layout::Layout2Point1, AV_CH_LAYOUT_2POINT1)
		WRITER_CASE(cmn::AudioChannel::Layout::Layout21, AV_CH_LAYOUT_2_1)
		WRITER_CASE(cmn::AudioChannel::Layout::LayoutSurround, AV_CH_LAYOUT_SURROUND)
		WRITER_CASE(cmn::AudioChannel::Layout::Layout22, AV_CH_LAYOUT_2_2)
		WRITER_CASE(cmn::AudioChannel::Layout::Layout3Point1, AV_CH_LAYOUT_3POINT1)
		WRITER_CASE(cmn::AudioChannel::Layout::Layout4Point0, AV_CH_LAYOUT_4POINT0)
		WRITER_CASE(cmn::AudioChannel::Layout::Layout4Point1, AV_CH_LAYOUT_4POINT1)
		WRITER_CASE(cmn::AudioChannel::Layout::LayoutQuad, AV_CH_LAYOUT_QUAD)
		WRITER_CASE(cmn::AudioChannel::Layout::Layout5Point0, AV_CH_LAYOUT_5POINT0)
		WRITER_CASE(cmn::AudioChannel::Layout::Layout5Point1, AV_CH_LAYOUT_5POINT1)
		WRITER_CASE(cmn::AudioChannel::Layout::Layout5Point0Back, AV_CH_LAYOUT_5POINT0_BACK)
		WRITER_CASE(cmn::AudioChannel::Layout::Layout5Point1Back, AV_CH_LAYOUT_5POINT1_BACK)
		WRITER_CASE(cmn::AudioChannel::Layout::Layout6Point0, AV_CH_LAYOUT_6POINT0)
		WRITER_CASE(cmn::AudioChannel::Layout::Layout6Point0Front, AV_CH_LAYOUT_6POINT0_FRONT)
		WRITER_CASE(cmn::AudioChannel::Layout::LayoutHexagonal, AV_CH_LAYOUT_HEXAGONAL)
		WRITER_CASE(cmn::AudioChannel::Layout::Layout6Point1, AV_CH_LAYOUT_6POINT1)
		WRITER_CASE(cmn::AudioChannel::Layout::Layout6Point1Back, AV_CH_LAYOUT_6POINT1_BACK)
		WRITER_CASE(cmn::AudioChannel::Layout::Layout6Point1Front, AV_CH_LAYOUT_6POINT1_FRONT)
		WRITER_CASE(cmn::AudioChannel::Layout::Layout7Point0, AV_CH_LAYOUT_7POINT0)
		WRITER_CASE(cmn::AudioChannel::Layout::Layout7Point0Front, AV_CH_LAYOUT_7POINT0_FRONT)
		WRITER_CASE(cmn::AudioChannel::Layout::Layout7Point1, AV_CH_LAYOUT_7POINT1)
		WRITER_CASE(cmn::AudioChannel::Layout::Layout7Point1Wide, AV_CH_LAYOUT_7POINT1_WIDE)
		WRITER_CASE(cmn::AudioChannel::Layout::Layout7Point1WideBack, AV_CH_LAYOUT_7POINT1_WIDE_BACK)
		WRITER_CASE(cmn::AudioChannel::Layout::LayoutOctagonal, AV_CH_LAYOUT_OCTAGONAL)
	}

	return 0;
}

bool Writer::FillCodecParameters(const std::shared_ptr<const Track> &track, AVCodecParameters *codec_parameters)
{
	auto media_track = track->track;

	if (media_track == nullptr)
	{
		return false;
	}

	switch (media_track->GetMediaType())
	{
		case cmn::MediaType::Unknown:
			return false;

		case cmn::MediaType::Data:
			[[fallthrough]];
		case cmn::MediaType::Subtitle:
			[[fallthrough]];
		case cmn::MediaType::Attachment:
			[[fallthrough]];
		case cmn::MediaType::Nb:
			break;

		case cmn::MediaType::Video: {
			codec_parameters->codec_type = AVMEDIA_TYPE_VIDEO;
			codec_parameters->codec_id = AvCodecIdFromMediaCodecId(media_track->GetCodecId());
			codec_parameters->bit_rate = media_track->GetBitrate();
			codec_parameters->width = media_track->GetWidth();
			codec_parameters->height = media_track->GetHeight();
			codec_parameters->format = (int)ffmpeg::compat::ToAVPixelFormat(media_track->GetColorspace());

			std::shared_ptr<const ov::Data> extra_data = nullptr;
			if (media_track->GetCodecId() == cmn::MediaCodecId::H265)
			{
				codec_parameters->codec_tag = MKTAG('h', 'v', 'c', '1');
				extra_data = media_track->GetDecoderConfigurationRecord() != nullptr ? media_track->GetDecoderConfigurationRecord()->GetData() : nullptr;
			}
			else if (media_track->GetCodecId() == cmn::MediaCodecId::H264)
			{
				codec_parameters->codec_tag = MKTAG('a', 'v', 'c', '1');
				extra_data = media_track->GetDecoderConfigurationRecord() != nullptr ? media_track->GetDecoderConfigurationRecord()->GetData() : nullptr;
			}
			else
			{
				codec_parameters->codec_tag = 0;
			}

			if (extra_data != nullptr)
			{
				codec_parameters->extradata_size = extra_data->GetLength();
				codec_parameters->extradata = static_cast<uint8_t *>(::av_malloc(codec_parameters->extradata_size + AV_INPUT_BUFFER_PADDING_SIZE));
				::memset(codec_parameters->extradata, 0, codec_parameters->extradata_size + AV_INPUT_BUFFER_PADDING_SIZE);
				::memcpy(codec_parameters->extradata, extra_data->GetData(), codec_parameters->extradata_size);
			}

			// _output_format->video_codec = codec_parameters->codec_id;

			break;
		}

		case cmn::MediaType::Audio: {
			codec_parameters->codec_type = AVMEDIA_TYPE_AUDIO;
			codec_parameters->codec_id = AvCodecIdFromMediaCodecId(media_track->GetCodecId());
			codec_parameters->bit_rate = media_track->GetBitrate();
			::av_channel_layout_default(&codec_parameters->ch_layout, media_track->GetChannel().GetCounts());
			codec_parameters->sample_rate = media_track->GetSample().GetRateNum();
			codec_parameters->frame_size = 1024;
			codec_parameters->format = static_cast<int>(media_track->GetSample().GetFormat());
			codec_parameters->codec_tag = 0;

			std::shared_ptr<const ov::Data> extra_data = nullptr;
			if (media_track->GetCodecId() == cmn::MediaCodecId::Aac)
			{
				codec_parameters->codec_tag = MKTAG('a', 'a', 'c', 'p');
				extra_data = media_track->GetDecoderConfigurationRecord() != nullptr ? media_track->GetDecoderConfigurationRecord()->GetData() : nullptr;
			}

			if (extra_data != nullptr)
			{
				codec_parameters->extradata_size = extra_data->GetLength();
				codec_parameters->extradata = static_cast<uint8_t *>(::av_malloc(codec_parameters->extradata_size + AV_INPUT_BUFFER_PADDING_SIZE));
				::memset(codec_parameters->extradata, 0, codec_parameters->extradata_size + AV_INPUT_BUFFER_PADDING_SIZE);
				::memcpy(codec_parameters->extradata, extra_data->GetData(), codec_parameters->extradata_size);
			}

			// _output_format->audio_codec = codec_parameters->codec_id;

			break;
		}
	}

	return true;
}

int Writer::OnWrite(const uint8_t *buf, int buf_size)
{
	logap("Writing %d bytes", buf_size);

	if (buf_size < 0)
	{
		// ffmpeg passed invalid buffer size
		logae("Invalid buffer size: %d", buf_size);
		OV_ASSERT2(false);
		return -1;
	}

	auto data_stream = _data_stream;
	if (data_stream != nullptr)
	{
		auto data = data_stream->GetData();
		auto remained = data->GetCapacity() - data->GetLength();
		if (remained < static_cast<size_t>(buf_size))
		{
			// In order to prevent CPU usage from increasing due to frequent memory allocation,
			// if the remaining buffer size is insufficient, it is allocated in advance.
			auto increase_amount = std::max(buf_size, WRITER_BUFFER_SIZE_INCREMENT);
#if DEBUG
			int current_buffer_size = _buffer_size;

			logat("Increasing buffer size by %d (before: %d, after: %d), data size: %zu (remained: %zu), requested: %d",
				  increase_amount, current_buffer_size, current_buffer_size + increase_amount,
				  data->GetLength(), remained, buf_size);
#endif	// DEBUG

			_buffer_size += increase_amount;

			auto data = data_stream->GetData();
			data->Reserve(_buffer_size);
		}

		if (data_stream->Write(buf, buf_size))
		{
			return buf_size;
		}
	}

	return -1;
}

int64_t Writer::OnSeek(int64_t offset, int whence)
{
	auto data_stream = _data_stream;
	auto data = data_stream->GetData();

	int64_t new_offset = -1;
	int64_t length = static_cast<int64_t>(data->GetLength());

	switch (whence)
	{
		case SEEK_SET:
			new_offset = offset;
			break;

		case SEEK_CUR:
			new_offset = data_stream->GetOffset() + offset;
			break;

		case SEEK_END:
			new_offset = length + offset;
			break;
	}

	if (data_stream->SetOffset(new_offset) == false)
	{
		// Invalid seek offset
		return -1;
	}

	return 0;
}

bool Writer::AddTrack(const std::shared_ptr<const MediaTrack> &media_track)
{
	if (media_track == nullptr)
	{
		return false;
	}

	auto lock_guard = std::lock_guard(_track_mutex);

	auto track = std::make_shared<Track>(_track_list.size(), RationalFromTimebase(media_track->GetTimeBase()), media_track);
	_track_list.emplace_back(track);
	_track_map[media_track->GetId()] = track;

	logat("Track %s is added", cmn::GetMediaTypeString(media_track->GetMediaType()));

	return true;
}

bool Writer::Prepare(ov::String service_name)
{
	if (_format_context != nullptr)
	{
		logae("Writer is already started");
		return false;
	}

	auto format_context = CreateFormatContext(_type);

	if (format_context == nullptr)
	{
		logae("Could not allocate format context");
		return false;
	}

	// Prepare AVStreams
	{
		auto lock_guard = std::lock_guard(_track_mutex);
		int index = 0;

		for (auto &track : _track_list)
		{
			auto stream = ::avformat_new_stream(format_context.get(), nullptr);

			if (stream == nullptr)
			{
				logae("Could not allocate stream");
				return false;
			}

			stream->id = index;
			stream->time_base = track->rational;

			if (FillCodecParameters(track, stream->codecpar) == false)
			{
				logae("Could not fill codec parameters");
				return false;
			}

			index++;
		}
	}

	auto avio_context = CreateAvioContext();

	if (avio_context == nullptr)
	{
		logae("Could not allocate avio context");
		return false;
	}

	format_context->pb = avio_context.get();

	::av_dict_set(&(format_context->metadata), "service_name", service_name.CStr(), 0);
	::av_dict_set(&(format_context->metadata), "service_provider", "OvenMediaEngine", 0);

	AVDictionary *options = nullptr;

	switch (_type)
	{
		case Type::MpegTs:
			break;

		case Type::M4s:
			// +dash: Creates data for DASH streaming
			// +frag_custom: Writes packets when flushing is requested
			// +skip_trailer: No trailer
			::av_dict_set(&options, "movflags", "+dash+frag_custom+skip_trailer", 0);
			::av_dict_set(&options, "brand", "mp42", 0);
			// ::av_dict_set(&options, "movflags", "+dash+frag_custom+delay_moov+skip_trailer", 0);
			// ::av_dict_set_int(&options, "use_editlist", 1, 0);
			break;
	}

	int result = ::avformat_init_output(format_context.get(), &options);

	if (result < 0)
	{
		logae("Could not init output: %s", StringFromError(result).CStr());
		return false;
	}

	// ::av_dump_format(format_context.get(), 0, "Writer", 1);

	if (ResetData() == false)
	{
		return false;
	}

	result = ::avformat_write_header(format_context.get(), &_options);
	if (result < 0)
	{
		logae("Could not write header: %s", StringFromError(result).CStr());
		return false;
	}

	_format_context = format_context;
	_avio_context = avio_context;
	_output_format = format_context->oformat;

	return true;
}

bool Writer::PrepareIfNeeded(ov::String service_name)
{
	if (_format_context != nullptr)
	{
		return true;
	}

	return Prepare(service_name);
}

int Writer::DecideBufferSize() const
{
	int estimated_buffer_size = 0;

	for (auto track : _track_list)
	{
		auto media_track = track->track;
		bool failed = false;

		switch (media_track->GetMediaType())
		{
			case cmn::MediaType::Video:
				[[fallthrough]];
			case cmn::MediaType::Audio: {
				auto bitrate = track->track->GetBitrate();

				if (bitrate > 0)
				{
					estimated_buffer_size += bitrate;
				}
				else
				{
					estimated_buffer_size = 0;
					failed = true;
				}
			}
			break;

			default:
				break;
		}

		if (failed)
		{
			break;
		}
	}

	if (estimated_buffer_size > 0)
	{
		int buffer_size = static_cast<int>(estimated_buffer_size * WRITER_BUFFER_ROOM_MULTIPLIER);
		// Since the bitrate can vary little by little, a certain amount of room should be given
		logat("Calculated buffer size from track list: %d, this buffer size will be used: %d",
			  estimated_buffer_size, buffer_size);

		return buffer_size;
	}

	// Since the buffer size cannot be inferred from the track list, the default size is used
	logat("Default buffer size is used: %d", WRITER_DEFAULT_BUFFER_SIZE);
	return WRITER_DEFAULT_BUFFER_SIZE;
}

bool Writer::ResetData(int track_id)
{
	{
		auto lock_guard = std::lock_guard(_track_mutex);

		if (_buffer_size == -1)
		{
			_buffer_size = DecideBufferSize();
		}

		if (track_id == -1)
		{
			// Reset all tracks
			for (auto track : _track_list)
			{
				track->Reset();
			}
		}
		else
		{
			// Reset the designated track
			auto item = _track_map.find(track_id);

			if (item == _track_map.end())
			{
				return false;
			}

			item->second->Reset();
		}
	}

	_data_stream = std::make_shared<ov::ByteStream>(std::make_shared<ov::Data>(_buffer_size));

	return true;
}

std::shared_ptr<const ov::Data> Writer::GetData() const
{
	auto data_stream = _data_stream;

	if (data_stream != nullptr)
	{
		return data_stream->GetDataPointer();
	}

	return nullptr;
}

off_t Writer::GetCurrentOffset() const
{
	auto data_stream = _data_stream;

	if (data_stream != nullptr)
	{
		return data_stream->GetOffset();
	}

	return 0;
}

int64_t Writer::GetFirstPts(uint32_t track_id) const
{
	auto lock_guard = std::lock_guard(_track_mutex);
	auto track_item = _track_map.find(track_id);

	if (track_item != _track_map.end())
	{
		auto &track = track_item->second;
		return (track->first_packet_received) ? track->first_pts : 0L;
	}

	return 0L;
}

int64_t Writer::GetFirstPts(cmn::MediaType type) const
{
	auto lock_guard = std::lock_guard(_track_mutex);
	for (auto &track_item : _track_list)
	{
		if (track_item->track->GetMediaType() == type)
		{
			return track_item->first_pts;
		}
	}

	return 0L;
}

int64_t Writer::GetFirstPts() const
{
	auto lock_guard = std::lock_guard(_track_mutex);
	if (_track_list.size() > 0)
	{
		auto &track = _track_list[0];
		return (track->first_packet_received) ? track->first_pts : 0L;
	}

	return 0L;
}

int64_t Writer::GetDuration(uint32_t track_id) const
{
	auto lock_guard = std::lock_guard(_track_mutex);
	auto track_item = _track_map.find(track_id);

	if (track_item != _track_map.end())
	{
		return track_item->second->duration;
	}

	return 0L;
}

int64_t Writer::GetDuration() const
{
	auto lock_guard = std::lock_guard(_track_mutex);
	if (_track_list.size() > 0)
	{
		return _track_list[0]->duration;
	}

	return 0L;
}

bool Writer::WritePacket(const std::shared_ptr<const MediaPacket> &packet, const std::shared_ptr<const ov::Data> &data, const std::vector<size_t> &length_list, size_t skip_count, size_t split_count)
{
	auto format_context = _format_context;

	std::shared_ptr<Writer::Track> track;

	{
		auto lock_guard = std::lock_guard(_track_mutex);
		auto track_item = _track_map.find(packet->GetTrackId());

		if (track_item == _track_map.end())
		{
			OV_ASSERT2(false);
			logac("Could not find the track in _track_map: %d (%zu)", packet->GetTrackId(), _track_map.size());
			return false;
		}

		track = track_item->second;

		OV_ASSERT2(track != nullptr);
	}

	if (data == nullptr)
	{
		logte("Could not convert packet: %d (writer type: %d)",
			  static_cast<int>(packet->GetBitstreamFormat()),
			  static_cast<int>(_type));

		return false;
	}

	int stream_index = track->stream_index;

	if ((format_context == nullptr) || (stream_index < 0) || (stream_index >= static_cast<int>(format_context->nb_streams)))
	{
		OV_ASSERT2(false);
		logac("Format context: %p, Stream index: %d, Number of streams: %u", format_context.get(), stream_index, format_context->nb_streams);
		return false;
	}

	if ((length_list.size() == 0) || (split_count == 0))
	{
		OV_ASSERT2(false);
		logac("length_list must contains at least 1 item");
		return false;
	}

	AVStream *stream = format_context->streams[stream_index];
	auto pts = packet->GetPts();
	auto dts = packet->GetDts();
	auto duration_per_packet = packet->GetDuration() / split_count;
	pts += (duration_per_packet * skip_count);
	dts += (duration_per_packet * skip_count);
	// It's muxing only, so we can change the constness temporary
	auto buffer = const_cast<uint8_t *>(data->GetDataAs<uint8_t>());

	for (auto length : length_list)
	{
		AVPacket av_packet = {0};

		av_packet.stream_index = stream_index;
		av_packet.flags = (packet->GetFlag() == MediaPacketFlag::Key) ? AV_PKT_FLAG_KEY : 0;
		av_packet.pts = pts;
		av_packet.dts = dts;
		av_packet.size = length;
		av_packet.duration = duration_per_packet;
		av_packet.data = buffer;

		// logaw("#%d [%s] Writing a packet: %15ld, %15ld (dur: %ld, %zu)",
		// 	  track->track->GetId(), (track->track->GetMediaType() == cmn::MediaType::Video) ? "V" : "A",
		// 	  pts, dts, duration_per_packet, length_list.size());

		::av_packet_rescale_ts(&av_packet, track->rational, stream->time_base);

		// LogPacket(&av_packet);

		int result = ::av_interleaved_write_frame(format_context.get(), &av_packet);

		if (result != 0)
		{
			logae("Could not write the frame: %s", StringFromError(result).CStr());

			return false;
		}

		buffer += length;
		pts += duration_per_packet;
		dts += duration_per_packet;

		track->duration += duration_per_packet;

		if (track->first_packet_received == false)
		{
			track->first_pts = pts;
			track->first_packet_received = true;
		}
	}

	return true;
}

bool Writer::WritePacket(const std::shared_ptr<const MediaPacket> &packet)
{
	std::shared_ptr<const ov::Data> data;
	std::vector<size_t> length_list;

	switch (packet->GetBitstreamFormat())
	{
		case cmn::BitstreamFormat::H264_AVCC:
			[[fallthrough]];
		case cmn::BitstreamFormat::HVCC:
			data = packet->GetData();
			length_list.push_back(data->GetLength());
			break;

		case cmn::BitstreamFormat::H264_ANNEXB:
			data = packet->GetData();
			data = NalStreamConverter::ConvertAnnexbToXvcc(data, packet->GetFragHeader());
			if (data == nullptr)
			{
				logte("Could not convert packet: %d (writer type: %d)",
					  static_cast<int>(packet->GetBitstreamFormat()),
					  static_cast<int>(_type));

				return false;
			}
			length_list.push_back(data->GetLength());
			break;

		case cmn::BitstreamFormat::H265_ANNEXB:
			data = packet->GetData();
			data = NalStreamConverter::ConvertAnnexbToXvcc(data, packet->GetFragHeader());
			if (data == nullptr)
			{
				logte("Could not convert packet: %d (writer type: %d)",
					  static_cast<int>(packet->GetBitstreamFormat()),
					  static_cast<int>(_type));

				return false;
			}
			length_list.push_back(data->GetLength());
			break;

		case cmn::BitstreamFormat::AAC_RAW:
			data = packet->GetData();
			length_list.push_back(data->GetLength());
			break;

		case cmn::BitstreamFormat::AAC_ADTS:
			if (_type != Type::M4s)
			{
				data = packet->GetData();
				length_list.push_back(data->GetLength());
			}
			else
			{
				data = AacConverter::ConvertAdtsToRaw(packet->GetData(), &length_list);
				if (data == nullptr)
				{
					logte("Could not convert packet: %d (writer type: %d)",
						  static_cast<int>(packet->GetBitstreamFormat()),
						  static_cast<int>(_type));

					return false;
				}
			}
			break;

		case cmn::BitstreamFormat::Unknown:
			[[fallthrough]];
		case cmn::BitstreamFormat::H264_RTP_RFC_6184:
			[[fallthrough]];
		case cmn::BitstreamFormat::H265_RTP_RFC_7798:
			[[fallthrough]];
		case cmn::BitstreamFormat::VP8:
			[[fallthrough]];
		case cmn::BitstreamFormat::VP8_RTP_RFC_7741:
			[[fallthrough]];
		case cmn::BitstreamFormat::AAC_MPEG4_GENERIC:
			[[fallthrough]];
		case cmn::BitstreamFormat::AAC_LATM:
			[[fallthrough]];
		case cmn::BitstreamFormat::OPUS:
			[[fallthrough]];
		case cmn::BitstreamFormat::OPUS_RTP_RFC_7587:
			[[fallthrough]];
		case cmn::BitstreamFormat::JPEG:
			[[fallthrough]];
		case cmn::BitstreamFormat::PNG:
			[[fallthrough]];
		case cmn::BitstreamFormat::WEBP:
			[[fallthrough]];			
		case cmn::BitstreamFormat::ID3v2:
			[[fallthrough]];
		case cmn::BitstreamFormat::MP3:
			[[fallthrough]];
		case cmn::BitstreamFormat::OVEN_EVENT:
			[[fallthrough]];
		case cmn::BitstreamFormat::CUE:
			[[fallthrough]];
		case cmn::BitstreamFormat::AMF:
			[[fallthrough]];
		case cmn::BitstreamFormat::SEI:
			[[fallthrough]];
		case cmn::BitstreamFormat::SCTE35:
			[[fallthrough]];
		case cmn::BitstreamFormat::WebVTT:	
			break;
	}

	return WritePacket(packet, data, length_list, 0, length_list.size());
}

bool Writer::Flush()
{
	auto format_context = _format_context;

	if (format_context != nullptr)
	{
		int result = ::av_write_frame(format_context.get(), nullptr);
		::avio_flush(format_context->pb);

		if (result >= 0)
		{
			return true;
		}

		logae("%s", ::StringFromError(result).CStr());
	}
	else
	{
		logae("Format context is nullptr");
	}

	return false;
}

std::shared_ptr<const ov::Data> Writer::Finalize()
{
	auto format_context = _format_context;
	if (format_context != nullptr)
	{
		Flush();

		::av_write_trailer(format_context.get());
	}

	_format_context = nullptr;
	_avio_context = nullptr;
	_output_format = nullptr;

	auto data_stream = _data_stream;
	_data_stream = nullptr;

	if (data_stream != nullptr)
	{
		auto data = data_stream->GetDataPointer();

		return data;
	}

	return nullptr;
}
