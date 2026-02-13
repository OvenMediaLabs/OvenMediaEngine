#include "writer.h"

#include <base/modules/data_format/scte35_event/scte35_event.h>
#include <modules/bitstream/aac/aac_converter.h>
#include <modules/bitstream/nalu/nal_stream_converter.h>
#include <modules/bitstream/opus/opus_specific_config.h>
#include <modules/ffmpeg/compat.h>
#include <modules/rtmp/amf0/amf_document.h>

#define OV_LOG_TAG "FFmpegWriter"

namespace ffmpeg
{
	std::shared_ptr<Writer> Writer::Create()
	{
		auto object = std::make_shared<Writer>();

		return object;
	}

	Writer::Writer()
		: _state(WriterStateNone)
	{
	}

	Writer::~Writer()
	{
		Stop();
	}

	void Writer::SetState(WriterState state)
	{
		_state.store(state);
	}

	Writer::WriterState Writer::GetState()
	{
		return _state.load();
	}

	void Writer::SetErrorMessage(const ov::String &message)
	{
		_error_message = message;
	}

	ov::String Writer::GetErrorMessage() const
	{
		return _error_message;
	}

	int Writer::InterruptCallback(void *opaque)
	{
		Writer *writer = (Writer *)opaque;
		if(writer == nullptr)
		{
			logte("Writer is null. stop the writer.");
			return 1;
		}

		auto ellapse = std::chrono::high_resolution_clock::now() - writer->GetLastPacketSentTime();
		if(writer->GetState() == WriterStateClosed)
		{
			logte("Writer is closed. stop the writer.");
			return 1;
		}
		else if(writer->GetState() == WriterStateConnecting)
		{
			auto ellapse_ms = std::chrono::duration_cast<std::chrono::milliseconds>(ellapse).count();
			if(ellapse_ms > writer->GetConnectionTimeout())
			{
				logte("connection timeout occurred. stop the writer. %d milliseconds. ", ellapse_ms);
				return 1;
			}
		}
		else if(writer->GetState() == WriterStateConnected)
		{
			auto ellapse_ms = std::chrono::duration_cast<std::chrono::milliseconds>(ellapse).count();
			if(ellapse_ms > writer->GetSendTimeout())
			{
				logte("Send timeout occurred. stop the writer. %d milliseconds. ", ellapse_ms);
				return 1;
			}
		}
		else if(writer->GetState() == WriterStateClosing)
		{
			// In closing state, Ignore interrupt to finish flushing and closing properly.
		}

		return 0;
	}

	bool Writer::SetUrl(const ov::String url, const ov::String format)
	{
		if (!url || url.IsEmpty() == true)
		{
			SetErrorMessage("Destination url is empty");
			return false;
		}

		_url = url;
		_format = format;

		// Create output context
		AVFormatContext *av_format = nullptr;
		int error = avformat_alloc_output_context2(&av_format, nullptr, (_format != nullptr) ? _format.CStr() : nullptr, _url.CStr());
		if (error < 0)
		{
			SetErrorMessage(ffmpeg::compat::AVErrorToString(error));
			return false;
		}

		SetAVFormatContext(av_format);

		return true;
	}

	ov::String Writer::GetUrl()
	{
		return _url;
	}

	void Writer::SetTimestampMode(TimestampMode mode)
	{
		_timestamp_mode = mode;
	}

	Writer::TimestampMode Writer::GetTimestampMode()
	{
		return _timestamp_mode;
	}

	std::shared_ptr<AVStream> Writer::CreateAVStream(const std::shared_ptr<MediaTrack> &media_track)
	{
		auto av_format = GetAVFormatContext();
		if (!av_format)
		{
			SetErrorMessage("Context is not available");
			return nullptr;
		}

		AVStream *new_stream = avformat_new_stream(av_format.get(), nullptr);
		if (!new_stream)
		{
			SetErrorMessage("Could not allocate stream");
			return nullptr;
		}

		std::shared_ptr<AVStream> av_stream(new_stream, [](AVStream *av_stream) {});
		if (!av_stream)
		{
			SetErrorMessage("Could not create AVStream");
			return nullptr;
		}

		// Convert MediaTrack to AVStream
		if (ffmpeg::compat::ToAVStream(media_track, av_stream.get()) == false)
		{
			SetErrorMessage("Could not convert track info to AVStream");
			return nullptr;
		}

		return av_stream;
	}

	bool Writer::AddMediaTrack(const std::shared_ptr<MediaTrack> &media_track, const std::shared_ptr<AVStream> &av_stream)
	{
		std::lock_guard<std::shared_mutex> mlock(_track_map_lock);
		_av_track_map[media_track->GetId()] = std::make_pair(av_stream, media_track);

		logtt("Added %s track. id(%d), codec(%s), format(%s)",
			  cmn::GetMediaTypeString(media_track->GetMediaType()),
			  media_track->GetId(),
			  ffmpeg::compat::GetCodecName(av_stream->codecpar->codec_id).CStr(),
			  cmn::GetBitstreamFormatString(media_track->GetOriginBitstream()));

		return true;
	}

	bool Writer::AddEventTrack(const std::shared_ptr<MediaTrack> &media_track, const std::shared_ptr<AVStream> &av_stream, cmn::BitstreamFormat format)
	{
		std::lock_guard<std::shared_mutex> mlock(_track_map_lock);
		_event_track_map[std::make_pair(media_track->GetId(), format)] = std::make_pair(av_stream, media_track);

		logtt("Added %s track. id(%d), codec(%s), format(%s)",
			  cmn::GetMediaTypeString(media_track->GetMediaType()),
			  media_track->GetId(),
			  ffmpeg::compat::GetCodecName(av_stream->codecpar->codec_id).CStr(),
			  cmn::GetBitstreamFormatString(format));

		return true;
	}

	bool Writer::AddTrack(const std::shared_ptr<MediaTrack> &media_track)
	{
		// TODO: This code will move to mediarouter.
		if (media_track->GetCodecId() == cmn::MediaCodecId::Opus &&
			media_track->GetDecoderConfigurationRecord() == nullptr)
		{
			auto opus_config{std::make_shared<OpusSpecificConfig>(media_track->GetChannel().GetCounts(), media_track->GetSampleRate())};
			media_track->SetDecoderConfigurationRecord(opus_config);
		}

		// Video/Audio is mapped 1:1
		if (media_track->GetMediaType() == cmn::MediaType::Video ||
			media_track->GetMediaType() == cmn::MediaType::Audio)
		{
			// Audio or Video stream track
			auto av_stream = CreateAVStream(media_track);
			if (!av_stream)
			{
				return false;
			}

			if (!AddMediaTrack(media_track, av_stream))
			{
				return false;
			}
		}

		// Data tracks are handled differently depending on the Bitstream format.


		else if (media_track->GetMediaType() == cmn::MediaType::Data)
		{
			// FLV(RTMP Push) - AMF0/AMF3 Metadata
			if (_output_format_name == "flv")
			{
				// AMF0/AMF3 event stream track
				auto av_stream = CreateAVStream(media_track);
				if (!av_stream)
				{
					return false;
				}

				if (!AddEventTrack(media_track, av_stream, cmn::BitstreamFormat::AMF))
				{
					return false;
				}
			}
			
			// MP4(MPEG-4 File) -  Data is not used
			if (_output_format_name == "mp4")
			{
				// Data track is not used in MP4
			}
		}

		return true;
	}

	int32_t Writer::GetTrackCountByType(cmn::MediaType media_type)
	{
		std::shared_lock<std::shared_mutex> mlock(_track_map_lock);
		auto track_map = _av_track_map;
		mlock.unlock();

		int32_t count = 0;
		for (const auto &[track_id, track_pair] : track_map)
		{
			auto &media_track = track_pair.second;
			if (media_track->GetMediaType() == media_type)
			{
				count++;
			}
		}

		return count;
	}

	std::shared_ptr<MediaTrack> Writer::GetTrackByTrackId(int32_t media_track_id) const
	{
		std::shared_lock<std::shared_mutex> mlock(_track_map_lock);
		auto track_map = _av_track_map;
		mlock.unlock();

		if (track_map.find(media_track_id) != track_map.end())
		{
			return track_map[media_track_id].second;
		}

		return nullptr;
	}

	bool Writer::Start()
	{
		SetState(WriterStateConnecting);

		_start_time = -1LL;
		_need_to_flush = false;
		_need_to_close = false;

		auto av_format = GetAVFormatContext();
		if (av_format == nullptr)
		{
			SetErrorMessage("Context is not available");
			return false;
		}

		// Set service & provider metadata
		av_dict_set(&av_format->metadata, "service_provider", "OvenMediaEngine", 0);
		av_dict_set(&av_format->metadata, "service_name", "OvenMediaEngine", 0);

		av_format->flush_packets = 1;

		// Set Interrupt Callback
		_interrupt_cb = {InterruptCallback, this};

		// The codec_tag value added in the ffmpeg::compat::ToAVStream function is removed when using the RTMP (FLV) format
		// Related log. "Tag avc1 incompatible with output codec id '27'"
		for (uint32_t i = 0; i < av_format->nb_streams; i++)
		{
			if (strcmp(av_format->oformat->name, "flv") == 0 && av_format->streams[i]->codecpar != nullptr)
			{
				av_format->streams[i]->codecpar->codec_tag = 0;
			}
		}

		if (!(av_format->oformat->flags & AVFMT_NOFILE))
		{
			_last_packet_sent_time = std::chrono::high_resolution_clock::now();
			int error = avio_open2(&av_format->pb, av_format->url, AVIO_FLAG_WRITE, &_interrupt_cb, nullptr);
			if (error < 0)
			{
				SetState(WriterStateError);

				SetErrorMessage(ffmpeg::compat::AVErrorToString(error));

				return false;
			}
		}

		_need_to_close = true;

		// Write header
		_last_packet_sent_time = std::chrono::high_resolution_clock::now();

		// Disable edts box
		AVDictionary *format_options = nullptr;
		av_dict_set_int(&format_options, "use_editlist", 0, 0);

		if (avformat_write_header(av_format.get(), &format_options) < 0)
		{
			SetState(WriterStateError);
			SetErrorMessage("Could not create header");

			return false;
		}

		_need_to_flush = true;

		SetState(WriterStateConnected);
		
		return true;
	}

	bool Writer::Stop()
	{
		// Added a state to ignore interrupts during closing.
		SetState(WriterStateClosing);

		ReleaseAVFormatContext();
		
		SetState(WriterStateClosed);
				
		return true;
	}

	void Writer::SetConnectionTimeout(int32_t timeout_ms)
	{
		_connection_timeout = timeout_ms;
	}

	int32_t Writer::GetConnectionTimeout() const
	{
		return _connection_timeout;
	}

	void Writer::SetSendTimeout(int32_t timeout_ms)
	{
		_send_timeout = timeout_ms;
	}

	int32_t Writer::GetSendTimeout() const
	{
		return _send_timeout;
	}

	bool Writer::SendPacket(const std::shared_ptr<MediaPacket> &packet, uint64_t *sent_bytes)
	{
		// Writer is not connected, but it's not an error. Waiting for initialization.
		if(GetState() != WriterStateConnected)
		{
			logtw("Writer is not initialized. state:%s", WriterStateToString(GetState()));
			return true;
		}

		if (!packet)
		{
			SetErrorMessage("Packet is null");
			return false;
		}

		// Drop packets that do not need to be transmitted
		auto [av_stream, media_track] = GetTrack(packet->GetTrackId(), packet->GetBitstreamFormat());
		if (av_stream == nullptr || media_track == nullptr)
		{
			return true;
		}

		// Start Timestamp
		if (_start_time == -1LL)
		{
			if (GetTimestampMode() == TIMESTAMP_STARTZERO_MODE)
			{
				_start_time = av_rescale_q(packet->GetPts(), AVRational{media_track->GetTimeBase().GetNum(), media_track->GetTimeBase().GetDen()}, AV_TIME_BASE_Q);
			}
			else if (GetTimestampMode() == TIMESTAMP_PASSTHROUGH_MODE)
			{
				_start_time = 0LL;
			}
		}

		// Convert 1/AV_TIME_BASE -> 1/TimeBase of MediaTrack
		int64_t start_time = av_rescale_q(_start_time, AV_TIME_BASE_Q, AVRational{media_track->GetTimeBase().GetNum(), media_track->GetTimeBase().GetDen()});

		// Convert MediaPacket to AVPacket
		AVPacket av_packet = {0};
		if (ToAVPacket(av_packet, av_stream, packet, media_track, start_time) == false)
		{
			SetErrorMessage("Failed to convert MediaPacket to AVPacket");
			return false;
		}

		// When a packet with a higher PTS from one of several tracks is sent first, the start time is set. 
		// If a later packet arrives but actually comes from an earlier time than the first one, it is dropped. 
		// But this is not treated as an error.
		if(av_packet.pts < 0 || av_packet.dts < 0 || av_packet.size <= 0)
		{
			logtt("To avoid negative timestamps, the packet is dropped. track:%d, pts:%lld, dts:%lld", media_track->GetId(), av_packet.pts, av_packet.dts);
			av_packet_unref(&av_packet);
			return true;
		}

		std::shared_ptr<const ov::Data> new_data = nullptr;

		if (_output_format_name == "flv")
		{
			// ANNEXB -> AVCC
			// ADTS -> RAW
			switch (packet->GetBitstreamFormat())
			{
				case cmn::BitstreamFormat::H264_AVCC:
					// Do nothing
					break;
				case cmn::BitstreamFormat::H264_ANNEXB: {
					new_data = NalStreamConverter::ConvertAnnexbToXvcc(packet->GetData(), packet->GetFragHeader());
					if (new_data == nullptr)
					{
						logtw("Failed to convert annexb to avcc");
						return false;
					}
					av_packet.size = new_data->GetLength();
					av_packet.data = (uint8_t *)new_data->GetDataAs<uint8_t>();
				}
				break;
				case cmn::BitstreamFormat::AAC_RAW:
					break;
				case cmn::BitstreamFormat::AAC_ADTS: {
					new_data = AacConverter::ConvertAdtsToRaw(packet->GetData(), nullptr);
					if (new_data == nullptr)
					{
						logtw("Failed to convert adts to raw");
						return false;
					}
					av_packet.size = new_data->GetLength();
					av_packet.data = (uint8_t *)new_data->GetDataAs<uint8_t>();
				}
				break;
				case cmn::BitstreamFormat::AMF: {
					// Related to 'com.youtube.cuepoint', 'textdata' event message
					ov::ByteStream byte_stream(packet->GetData());
					AmfDocument document;
					if (document.Decode(byte_stream) == false)
					{
						loge(OV_LOG_TAG".Events","Failed to decode AMF Event");
						return true;
					}

					logi(OV_LOG_TAG".Events","Inserted AMF Event. PTS: %lld, %s", packet->GetPts(), document.ToString().Replace("\n", " ").CStr());
				}
				break;
				default:
					// Unsupported bitstream foramt, but it is not an error.
					return true;
			}
		}
		else if (_output_format_name == "mp4")
		{
			switch (packet->GetBitstreamFormat())
			{
				case cmn::BitstreamFormat::HVCC:
					break;
				case cmn::BitstreamFormat::H265_ANNEXB: {
					new_data = NalStreamConverter::ConvertAnnexbToXvcc(packet->GetData(), packet->GetFragHeader());
					if (new_data == nullptr)
					{
						logtw("Failed to convert annexb to avcc");
						return false;
					}
					av_packet.size = new_data->GetLength();
					av_packet.data = (uint8_t *)new_data->GetDataAs<uint8_t>();
				}
				break;
				case cmn::BitstreamFormat::H264_AVCC:
					break;
				case cmn::BitstreamFormat::H264_ANNEXB: {
					new_data = NalStreamConverter::ConvertAnnexbToXvcc(packet->GetData(), packet->GetFragHeader());
					if (new_data == nullptr)
					{
						logtw("Failed to convert annexb to avcc");
						return false;
					}
					av_packet.size = new_data->GetLength();
					av_packet.data = (uint8_t *)new_data->GetDataAs<uint8_t>();
				}
				break;
				case cmn::BitstreamFormat::AAC_RAW:
				case cmn::BitstreamFormat::OPUS:
					break;
				case cmn::BitstreamFormat::AAC_ADTS: {
					new_data = AacConverter::ConvertAdtsToRaw(packet->GetData(), nullptr);
					if (new_data == nullptr)
					{
						logtw("Failed to convert adts to raw");
						return false;
					}
					av_packet.size = new_data->GetLength();
					av_packet.data = (uint8_t *)new_data->GetDataAs<uint8_t>();
				}
				break;

				default:
					// Unsupported bitstream foramt, but it is not an error.
					return true;
			}
		}
		else if (_output_format_name == "mpegts") // TS, SRT
		{
			switch(packet->GetBitstreamFormat())
			{
				case cmn::BitstreamFormat::H264_ANNEXB:
				case cmn::BitstreamFormat::H264_AVCC:
				case cmn::BitstreamFormat::H265_ANNEXB:
				case cmn::BitstreamFormat::HVCC:
				case cmn::BitstreamFormat::AAC_RAW:
				case cmn::BitstreamFormat::AAC_ADTS:
				case cmn::BitstreamFormat::AAC_LATM:
				case cmn::BitstreamFormat::OPUS:
				case cmn::BitstreamFormat::MP3:
					break;
				default:
					// Unsupported bitstream format, but it is not an error.
					return true;
			}
		}

		auto av_format = GetAVFormatContext();
		if (!av_format)
		{
			av_packet_unref(&av_packet);
			SetState(WriterStateError);
			SetErrorMessage("Context is not available");

			return false;
		}

		_last_packet_sent_time = std::chrono::high_resolution_clock::now();

		std::unique_lock<std::shared_mutex> mlock(_av_format_lock);

		// Check DTS monotonicity. if not, drop the packet.
		auto it = _track_last_dts_map.find(av_packet.stream_index);
		if (it != _track_last_dts_map.end())
		{
			if (av_packet.dts < it->second)
			{
				logtw("To avoid non-monotonic DTS, the packet is dropped. track:%d, pts:%lld, dts:%lld, last_dts:%lld",
					  media_track->GetId(),
					  av_packet.pts,
					  av_packet.dts,
					  it->second);
				av_packet_unref(&av_packet);
				return true;
			}
		}

		// Write packet
		int error = ::av_write_frame(av_format.get(), &av_packet);
		if (error != 0)
		{
			av_packet_unref(&av_packet);
			SetState(WriterStateError);
			SetErrorMessage(ffmpeg::compat::AVErrorToString(error));

			return false;
		}

		// Update last DTS for the track to handle DTS monotonicity
		_track_last_dts_map[av_packet.stream_index] = av_packet.dts;

		mlock.unlock();

		if (sent_bytes != nullptr)
		{
			*sent_bytes = av_packet.size;
		}

		return true;
	}

	std::chrono::high_resolution_clock::time_point Writer::GetLastPacketSentTime()
	{
		return _last_packet_sent_time;
	}

	std::shared_ptr<AVFormatContext> Writer::GetAVFormatContext() const
	{
		std::shared_lock<std::shared_mutex> mlock(_av_format_lock);
		return _av_format;
	}

	void Writer::SetAVFormatContext(AVFormatContext *av_format)
	{
		std::lock_guard<std::shared_mutex> mlock(_av_format_lock);
		// forward _need_to_flush and _need_to_close to lambda
		_av_format.reset(av_format, [&need_to_flush = _need_to_flush, &need_to_close = _need_to_close](AVFormatContext *av_format_ptr) {
			if (av_format_ptr == nullptr)
			{
				return;
			}

			if (need_to_flush)
			{
				av_write_trailer(av_format_ptr);
			}

			if (need_to_close && av_format_ptr->pb != nullptr)
			{
				avio_closep(&av_format_ptr->pb);
			}
			
			avformat_free_context(av_format_ptr);
		});

		// Set output format name
		_output_format_name = _av_format->oformat->name;
	}

	void Writer::ReleaseAVFormatContext()
	{
		std::lock_guard<std::shared_mutex> mlock(_av_format_lock);
		_av_format = nullptr;
		_need_to_flush = false;
		_need_to_close = false;
	}

	std::pair<std::shared_ptr<AVStream>, std::shared_ptr<MediaTrack>> Writer::GetTrack(int32_t track_id, cmn::BitstreamFormat format) const
	{
		std::shared_lock<std::shared_mutex> mlock(_track_map_lock);

		// Check track for Audio/Video first
		{
			auto it = _av_track_map.find(track_id);
			if (it != _av_track_map.end())
			{
				return it->second;
			}
		}

		// Check track for Data/Event
		{
			auto it = _event_track_map.find(std::make_pair(track_id, format));
			if (it != _event_track_map.end())
			{
				return it->second;
			}
		}

		return std::make_pair(nullptr, nullptr);
	}

	bool Writer::ToAVPacket(AVPacket &av_packet, const std::shared_ptr<AVStream> av_stream, const std::shared_ptr<MediaPacket> &media_packet, const std::shared_ptr<MediaTrack> &media_track, int64_t start_time)
	{
		if (av_stream == nullptr || media_packet == nullptr || media_packet->GetData() == nullptr || media_track == nullptr)
		{
			return false;
		}

		av_packet.stream_index = av_stream->index;
		av_packet.flags = (media_packet->GetFlag() == MediaPacketFlag::Key) ? AV_PKT_FLAG_KEY : 0;
		av_packet.pts = av_rescale_q(media_packet->GetPts() - start_time, AVRational{media_track->GetTimeBase().GetNum(), media_track->GetTimeBase().GetDen()}, av_stream->time_base);
		av_packet.dts = av_rescale_q(media_packet->GetDts() - start_time, AVRational{media_track->GetTimeBase().GetNum(), media_track->GetTimeBase().GetDen()}, av_stream->time_base);
		av_packet.duration = av_rescale_q(media_packet->GetDuration(), AVRational{media_track->GetTimeBase().GetNum(), media_track->GetTimeBase().GetDen()}, av_stream->time_base);
		av_packet.size = media_packet->GetDataLength();
		av_packet.data = (uint8_t *)media_packet->GetData()->GetDataAs<uint8_t>();

		return true;
	}
}  // namespace ffmpeg
