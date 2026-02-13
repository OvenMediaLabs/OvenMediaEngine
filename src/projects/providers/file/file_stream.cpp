//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Keukhan
//  Copyright (c) 2021 AirenSoft. All rights reserved.
//
//==============================================================================

#include "file_stream.h"

#include <base/info/application.h>
#include <base/ovlibrary/byte_io.h>
#include <modules/ffmpeg/compat.h>
#include <modules/rtp_rtcp/rtp_depacketizer_mpeg4_generic_audio.h>

#include "file_private.h"
#include "file_provider.h"

namespace pvd
{
	std::shared_ptr<FileStream> FileStream::Create(const std::shared_ptr<pvd::PullApplication> &application,
												   const uint32_t stream_id,
												   const ov::String &stream_name,
												   const std::vector<ov::String> &url_list,
												   const std::shared_ptr<pvd::PullStreamProperties> &properties)
	{
		info::Stream stream_info(*std::static_pointer_cast<info::Application>(application), StreamSourceType::File);

		stream_info.SetId(stream_id);
		stream_info.SetName(stream_name);

		auto stream = std::make_shared<FileStream>(application, stream_info, url_list, properties);
		if (!stream->PullStream::Start())
		{
			// Explicit deletion
			stream.reset();
			return nullptr;
		}

		return stream;
	}

	FileStream::FileStream(const std::shared_ptr<pvd::PullApplication> &application, const info::Stream &stream_info, const std::vector<ov::String> &url_list, const std::shared_ptr<pvd::PullStreamProperties> &properties)
		: pvd::PullStream(application, stream_info, url_list, properties)
	{
		SetState(State::IDLE);
	}

	FileStream::~FileStream()
	{
		PullStream::Stop();
		Release();
	}

	std::shared_ptr<pvd::FileProvider> FileStream::GetFileProvider()
	{
		return std::static_pointer_cast<FileProvider>(GetApplication()->GetParentProvider());
	}

	void FileStream::Release()
	{
	}

	bool FileStream::StartStream(const std::shared_ptr<const ov::Url> &url)
	{
		if(url == nullptr)
		{
			SetState(State::ERROR);
			return true;
		}
		
		// Only start from IDLE, ERROR, STOPPED
		if (!(GetState() == State::IDLE || GetState() == State::ERROR || GetState() == State::STOPPED))
		{
			return true;
		}

		_url = url;

		ov::StopWatch stop_watch;

		stop_watch.Start();
		if (ConnectTo() == false)
		{
			return false;
		}
		_origin_request_time_msec = stop_watch.Elapsed();

		stop_watch.Update();

		if (RequestDescribe() == false)
		{
			Release();
			return false;
		}

		if (RequestPlay() == false)
		{
			Release();
			return false;
		}

		_origin_response_time_msec = stop_watch.Elapsed();

		// Stream was created completely
		_stream_metrics = StreamMetrics(*std::static_pointer_cast<info::Stream>(PullStream::GetSharedPtr()));
		if (_stream_metrics != nullptr)
		{
			_stream_metrics->SetOriginConnectionTimeMSec(_origin_request_time_msec);
			_stream_metrics->SetOriginSubscribeTimeMSec(_origin_response_time_msec);
		}

		return true;
	}

	bool FileStream::RestartStream(const std::shared_ptr<const ov::Url> &url)
	{
		logti("[%s/%s(%u)] stream tries to reconnect to %s", GetApplicationTypeName(), GetName().CStr(), GetId(), url->ToUrlString().CStr());
		return StartStream(url);
	}

	bool FileStream::StopStream()
	{
		if (GetState() == State::STOPPED)
		{
			return true;
		}

		if (!RequestStop())
		{
			// Force terminate
			SetState(State::ERROR);
		}

		Release();

		return true;
	}

	bool FileStream::ConnectTo()
	{
		if (GetState() == State::PLAYING || GetState() == State::TERMINATED)
		{
			return false;
		}

		int err = 0;

		auto url = ov::String::FormatString("%s%s", GetApplicationInfo().GetConfig().GetProviders().GetFileProvider().GetRootPath().CStr(), _url->Path().CStr());

		_format_context = nullptr;
		logtt("%s/%s(%u) Trying to open file. path(%s)", GetApplicationInfo().GetVHostAppName().CStr(), GetName().CStr(), GetId(), url.CStr());
		if ((err = ::avformat_open_input(&_format_context, url.CStr(), nullptr, nullptr)) < 0)
		{
			SetState(State::ERROR);

			char errbuf[256];
			av_strerror(err, errbuf, sizeof(errbuf));

			logte("%s/%s(%u) Failed to open file : %s (%s)", GetApplicationInfo().GetVHostAppName().CStr(), GetName().CStr(), GetId(), errbuf, url.CStr());

			return false;
		}

		SetState(State::CONNECTED);

		return true;
	}

	bool FileStream::RequestDescribe()
	{
		if (GetState() != State::CONNECTED)
		{
			return false;
		}

		if (::avformat_find_stream_info(_format_context, NULL) < 0)
		{
			SetState(State::ERROR);
			logte("Could not find stream information");

			return false;
		}

		for (uint32_t track_id = 0; track_id < _format_context->nb_streams; track_id++)
		{
			auto stream = _format_context->streams[track_id];
			if (!stream)
			{
				continue;
			}

			auto media_track = std::make_shared<MediaTrack>();
			if (!media_track)
			{
				continue;
			}

			if (ffmpeg::compat::ToMediaTrack(stream, media_track) == false)
			{
				continue;
			}

#if FILE_FIXED_TRACK_ID 
			if(GetFirstTrackByType(media_track->GetMediaType()) == nullptr)
			{
				media_track->SetId(GetFixedTrackIdOfMediaType(media_track->GetMediaType()));
				AddTrack(media_track);
			}
			else{
				logtt("Duplicate media types are not used and ignored. %s", media_track->GetInfoString().CStr());
			}
#else
			AddTrack(media_track);
#endif
		}

		// If there is no data track, add a dummy data track
		if(GetFirstTrackByType(cmn::MediaType::Data) == nullptr)
		{
			auto data_track = std::make_shared<MediaTrack>();
			data_track->SetId(FILE_DATA_TRACK_ID);
			data_track->SetMediaType(cmn::MediaType::Data);
			data_track->SetTimeBase(1, 1000);
			data_track->SetOriginBitstream(cmn::BitstreamFormat::Unknown);
			
			AddTrack(data_track);
		}

		InitBaseTimestamp();

		SetState(State::DESCRIBED);

		return true;
	}

	bool FileStream::RequestPlay()
	{
		if (GetState() != State::DESCRIBED)
		{
			return false;
		}

		SetState(State::PLAYING);

		_play_request_time.Start();

		return true;
	}

	void FileStream::SendSequenceHeader()
	{
		if (_sent_sequence_header == true)
			return;

		for (uint32_t track_id = 0; track_id < _format_context->nb_streams; ++track_id)
		{
			AVStream *stream = _format_context->streams[track_id];

#if FILE_FIXED_TRACK_ID
			auto fix_track_id = GetFixedTrackIdOfMediaType(ffmpeg::compat::ToMediaType(stream->codecpar->codec_type));
			auto track = GetTrack(fix_track_id);
#else
			auto track = GetTrack(track_id);
#endif
			if (track == nullptr)
			{
				continue;
			}

			auto media_type = track->GetMediaType();
			auto codec_id = track->GetCodecId();

			if (codec_id == cmn::MediaCodecId::H264)
			{
				// @extradata == AVCDecoderConfigurationRecord
				auto media_packet = std::make_shared<MediaPacket>(
					GetMsid(),
					media_type,
					track->GetId(),
					std::make_shared<ov::Data>(stream->codecpar->extradata, stream->codecpar->extradata_size),
					0,
					0,
					-1LL,
					MediaPacketFlag::Unknown,
					cmn::BitstreamFormat::H264_AVCC,
					cmn::PacketType::SEQUENCE_HEADER);

				SendFrame(media_packet);
			}
			else if (codec_id == cmn::MediaCodecId::Aac)
			{
				// @extradata == AudioSpecificConfig
				auto media_packet = std::make_shared<MediaPacket>(
					GetMsid(),
					media_type,
					track->GetId(),
					std::make_shared<ov::Data>(stream->codecpar->extradata, stream->codecpar->extradata_size),
					0,
					0,
					-1LL,
					MediaPacketFlag::Unknown,
					cmn::BitstreamFormat::AAC_RAW,
					cmn::PacketType::SEQUENCE_HEADER);

				SendFrame(media_packet);
			}
		}

		_sent_sequence_header = true;
	}

	bool FileStream::RequestStop()
	{
		if (GetState() != State::PLAYING)
		{
			return false;
		}

		avformat_close_input(&_format_context);
		_format_context = nullptr;

		return true;
	}

	bool FileStream::RequestRewind()
	{
		if (GetState() != State::PLAYING)
		{
			return false;
		}

		if (::av_seek_frame(_format_context, -1, 0, 0) < 0)
		{
			return false;
		}

		return true;
	}

	PullStream::ProcessMediaResult FileStream::ProcessMediaPacket()
	{
		if (_format_context == nullptr)
		{
			return ProcessMediaResult::PROCESS_MEDIA_FAILURE;
		}

		AVPacket packet;
		packet.data = nullptr;
		packet.size = 0;

		SendSequenceHeader();

		while (true)
		{
			int32_t ret = ::av_read_frame(_format_context, &packet);
			if (ret < 0)
			{
				if (ret == AVERROR(EAGAIN))
				{
					return ProcessMediaResult::PROCESS_MEDIA_TRY_AGAIN;
				}
				else if ((ret == AVERROR_EOF || ::avio_feof(_format_context->pb)))
				{
					RequestRewind();

					UpdateBaseTimestamp();
					logtt("%s/%s(%u) Reached the end of the file. rewind to the first frame.", GetApplicationInfo().GetVHostAppName().CStr(), GetName().CStr(), GetId());
					continue;
				}

				// If the I/O is broken, terminate the thread.
				logte("%s/%s(%u) FileStream's I/O has broken.", GetApplicationInfo().GetVHostAppName().CStr(), GetName().CStr(), GetId());

				SetState(State::ERROR);
				
				return ProcessMediaResult::PROCESS_MEDIA_FAILURE;
			}

#if FILE_FIXED_TRACK_ID
			auto fix_track_id = GetFixedTrackIdOfMediaType(ffmpeg::compat::ToMediaType(_format_context->streams[packet.stream_index]->codecpar->codec_type));
			auto track = GetTrack(fix_track_id);
#else			
			auto track = GetTrack(packet.stream_index);
#endif			
			if (track == nullptr)
			{
				::av_packet_unref(&packet);

				return ProcessMediaResult::PROCESS_MEDIA_TRY_AGAIN;
			}

			cmn::BitstreamFormat bitstream_format = cmn::BitstreamFormat::Unknown;
			cmn::PacketType packet_type = cmn::PacketType::Unknown;
			switch (track->GetCodecId())
			{
				case cmn::MediaCodecId::H264:
					bitstream_format = cmn::BitstreamFormat::H264_AVCC;
					packet_type = cmn::PacketType::NALU;
					break;
				case cmn::MediaCodecId::Aac:
					bitstream_format = cmn::BitstreamFormat::AAC_RAW;
					packet_type = cmn::PacketType::RAW;
					break;
				case cmn::MediaCodecId::Opus:
					bitstream_format = cmn::BitstreamFormat::OPUS;
					packet_type = cmn::PacketType::RAW;
					break;
				default:
					::av_packet_unref(&packet);
					continue;
			}

			auto media_packet = ffmpeg::compat::ToMediaPacket(GetMsid(), track->GetId(), &packet, track->GetMediaType(), bitstream_format, packet_type);
			::av_packet_unref(&packet);

			// Calculate PTS/DTS + Base Timestamp
			UpdateTimestamp(media_packet);

			UpdateNextTimestamp(media_packet);

			// Send to MediaRouter
			SendFrame(std::move(media_packet));

			// Real-time processing - It treats the packet the same as the real time.
			if (_play_request_time.Elapsed() < (static_cast<int64_t>(static_cast<double>(media_packet->GetDts()) * track->GetTimeBase().GetExpr() * 1000)))
			{
				break;
			}
		}

		return ProcessMediaResult::PROCESS_MEDIA_SUCCESS;
	}
	
	void FileStream::InitBaseTimestamp()
	{
		for (const auto &[track_id, track] : GetTracks())
		{
			_base_timestamp[track_id] = GetBaseTimestamp(track_id);
		}
	}

	void FileStream::UpdateTimestamp(std::shared_ptr<MediaPacket> &packet)
	{
		int64_t base_timestamp = 0;
		if (_base_timestamp.find(packet->GetTrackId()) != _base_timestamp.end())
		{
			base_timestamp = _base_timestamp[packet->GetTrackId()];
		}

		packet->SetPts(base_timestamp + packet->GetPts());
		packet->SetDts(base_timestamp + packet->GetDts());
	}

	void FileStream::UpdateNextTimestamp(std::shared_ptr<MediaPacket> &packet)
	{
		_next_timestamp[packet->GetTrackId()] = packet->GetPts() + packet->GetDuration();
	}

	void FileStream::UpdateBaseTimestamp()
	{
		// Select the track with the highest timestamp value.
		int64_t highest_timestamp_ms = 0;
		
		for (const auto &[track_id, timestamp] : _next_timestamp)
		{
			auto track = GetTrack(track_id);
			if (track == nullptr)
			{
				continue;
			}

			auto timestamp_ms = (timestamp * 1000) / track->GetTimeBase().GetTimescale();

			highest_timestamp_ms = std::max<int64_t>(timestamp_ms, highest_timestamp_ms);
		}

		// Change the Base Timestamp of all tracks to the highest timestamp
		for (const auto &[track_id, timestamp] : _next_timestamp)
		{
			auto track = GetTrack(track_id);
			if (track == nullptr)
			{
				continue;
			}

			// avoid to non monotonically increasing dts problem
			auto timestamp_tb = highest_timestamp_ms * track->GetTimeBase().GetTimescale() / 1000;  

			_base_timestamp[track_id] = timestamp_tb;
			_next_timestamp[track_id] = timestamp_tb;
		}
	}

	uint8_t FileStream::GetFixedTrackIdOfMediaType(cmn::MediaType media_type)
	{
		switch (media_type)
		{
			case cmn::MediaType::Video:
				return FILE_VIDEO_TRACK_ID;
			case cmn::MediaType::Audio:
				return FILE_AUDIO_TRACK_ID;
			case cmn::MediaType::Data:
			default:
				return FILE_DATA_TRACK_ID;
		}
	}

}  // namespace pvd
