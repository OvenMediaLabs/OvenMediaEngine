//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2022 AirenSoft. All rights reserved.
//
//==============================================================================
#include "hls_stream.h"

#include <base/ovlibrary/hex.h>
#include <config/config_manager.h>
#include <modules/dump/dump.h>

#include <pugixml-1.9/src/pugixml.hpp>

#include <base/publisher/application.h>
#include <base/publisher/stream.h>

#include "hls_application.h"
#include "hls_session.h"
#include "hls_private.h"

/*

[Legacy HLS (Version 3) URLs]

* Master Playlist
http[s]://<host>:<port>/<application_name>/<stream_name>/ts:playlist.m3u8
http[s]://<host>:<port>/<application_name>/<stream_name>/playlist.m3u8?format=ts

* Media Playlist
http[s]://<host>:<port>/<application_name>/<stream_name>/medialist_<variant_name>_hls.m3u8

* Segment File
http[s]://<host>:<port>/<application_name>/<stream_name>/seg_<variant_name>_<number>_hls.ts


[LL-HLS URLs] for reference

* Master Playlist
http[s]://<host>:<port>/[<application_name>/]<stream_name>/playlist.m3u8

* Media Playlist
http[s]://<host>:<port>/[<application_name>/]<stream_name>/chunklist_*_llhls.m3u8

* Segment File
http[s]://<host>:<port>/[<application_name>/]<stream_name>/init_*_llhls.m4s
http[s]://<host>:<port>/[<application_name>/]<stream_name>/part_*_llhls.m4s
http[s]://<host>:<port>/[<application_name>/]<stream_name>/seg_*_llhls.m4s

*/

std::shared_ptr<HlsStream> HlsStream::Create(const std::shared_ptr<pub::Application> application, const info::Stream &info, uint32_t worker_count)
{
	auto stream = std::make_shared<HlsStream>(application, info, worker_count);
	return stream;
}

HlsStream::HlsStream(const std::shared_ptr<pub::Application> application, const info::Stream &info, uint32_t worker_count)
	: Stream(application, info), _worker_count(worker_count)
{
}

HlsStream::~HlsStream()
{
	logtt("TsStream(%s/%s) has been terminated finally", GetApplicationName(), GetName().CStr());
}

std::shared_ptr<const pub::Stream::DefaultPlaylistInfo> HlsStream::GetDefaultPlaylistInfo() const
{
	// Since the same value is always stored in info, it is not an issue
	// even if multiple instances of info are created due to a race conditio in multi-threading
	static auto info = []() -> std::shared_ptr<const pub::Stream::DefaultPlaylistInfo> {
		ov::String file_name = "playlist.m3u8";
		auto file_name_without_ext = file_name.Substring(0, file_name.IndexOfRev('.'));

		return std::make_shared<const pub::Stream::DefaultPlaylistInfo>(
			"hls_default",
			file_name_without_ext,
			file_name);
	}();

	return info;
}

ov::String HlsStream::GetStreamId() const
{
	return ov::String::FormatString("hlsv3/%s", GetUri().CStr());
}

bool HlsStream::CreateOriginSessionPool()
{
	if (_origin_mode == false)
	{
		return false;
	}

	size_t max_pool_size = _worker_count == 0 ? 1 : _worker_count;

	// Create sessions up to _worker_count
	for (size_t i = 0; i < max_pool_size; i++)
	{
		auto session = HlsSession::Create(static_cast<session_id_t>(i),
											_origin_mode,
											"",
											GetApplication(),
											pub::Stream::GetSharedPtr(),
											0);
		logtd("LLHlsStream(%s/%s) - Pre-created origin mode session in pool, session id: %zu", GetApplication()->GetVHostAppName().CStr(), GetName().CStr(), i);
		AddSession(session);
	}

	return true;
}

std::shared_ptr<HlsSession> HlsStream::GetSessionFromPool()
{
	// Max session pool size if _worker_count
	size_t max_pool_size = _worker_count == 0 ? 1 : _worker_count;
	
	// Get random index
	size_t index = ov::Random::GenerateUInt32() % max_pool_size;

	auto session = GetSession(static_cast<session_id_t>(index));
	if (session == nullptr)
	{
		return nullptr;
	}

	return std::static_pointer_cast<HlsSession>(session);
}

bool HlsStream::Start()
{
	if (GetState() != State::CREATED)
	{
		return false;
	}

	if (CreateStreamWorker(_worker_count) == false)
	{
		return false;
	}

	if (_origin_mode == true)
	{
		if (CreateOriginSessionPool() == false)
		{
			logte("HLS Stream(%s/%s) - Failed to create origin session pool", GetApplication()->GetVHostAppName().CStr(), GetName().CStr());
			return false;
		}
	}

	auto config = GetApplication()->GetConfig();
	_ts_config = config.GetPublishers().GetHlsPublisher();

	_default_option_rewind = _ts_config.GetDefaultQueryString().GetBoolValue("_HLS_rewind", kDefaultHlsRewind);

	if (_ts_config.ShouldCreateDefaultPlaylist() == true)
	{
		CreateDefaultPlaylist();
	}
	else
	{
		logti("HLS Stream(%s/%s) - Default playlist creation is disabled", GetApplication()->GetVHostAppName().CStr(), GetName().CStr());
		if (GetPlaylists().size() == 0)
		{
			logtw("HLS Stream(%s/%s) - There is no playlist to create packagers, HLSv3 will not work for this stream.", GetApplication()->GetVHostAppName().CStr(), GetName().CStr());
			Stop(); // Release resources
			return false;
		}
	}

	// Create Packetizer
	if (CreatePackagers() == false)
	{
		logte("Failed to create packetizers");
		Stop(); // Release resources
		return false;
	}

	logti("HLS Stream has been created : %s/%u\nSegment Duration(%.2f) Segment Count(%u)", GetName().CStr(), GetId(),
		  _ts_config.GetSegmentDuration(), _ts_config.GetSegmentCount());

	InitializeAllDumps();

	return Stream::Start();
}

bool HlsStream::Stop()
{
	logtt("TsStream(%s) has been stopped", GetName().CStr());

	{
		std::lock_guard<std::shared_mutex> lock(_ts_packetizers_guard);
		_ts_packetizers.clear();
		_track_packetizers.clear();
	}

	{
		std::lock_guard<std::shared_mutex> lock(_media_playlists_guard);
		_media_playlists.clear();
	}

	{
		std::lock_guard<std::shared_mutex> lock(_ts_packagers_guard);
		_ts_packagers.clear();
	}

	{
		std::lock_guard<std::shared_mutex> lock(_storage_map_guard);
		_storage_map.clear();
	}

	{
		std::lock_guard<std::shared_mutex> lock(_master_playlists_guard);
		_master_playlists.clear();

	}

	StopDumps();

	return Stream::Stop();
}

bool HlsStream::IsSupportedCodec(cmn::MediaCodecId codec_id) const
{
	switch (codec_id)
	{
		case cmn::MediaCodecId::H265:
		case cmn::MediaCodecId::H264:
		case cmn::MediaCodecId::Aac:
			return true;
		default:
			return false;
	}

	return false;
}

bool HlsStream::CreateDefaultPlaylist()
{
	std::shared_ptr<MediaTrack> first_video_track = nullptr, first_audio_track = nullptr;
	for (const auto &[id, track] : _tracks)
	{
		if (IsSupportedCodec(track->GetCodecId()) == true)
		{
			// For default llhls.m3u8
			if (first_video_track == nullptr && track->GetMediaType() == cmn::MediaType::Video)
			{
				first_video_track = track;
			}
			else if (first_audio_track == nullptr && track->GetMediaType() == cmn::MediaType::Audio)
			{
				first_audio_track = track;
			}
		}
		else if (track->GetMediaType() == cmn::MediaType::Data)
		{
			// Data track
			// Do nothing
		}
		else
		{
			logti("LLHlsStream(%s/%s) - Ignore unsupported codec(%s)", GetApplication()->GetVHostAppName().CStr(), GetName().CStr(), cmn::GetCodecIdString(track->GetCodecId()));
			continue;
		}
	}

	if (first_video_track == nullptr && first_audio_track == nullptr)
	{
		logtw("HLS stream [%s/%s] could not create default playlist.m3u8 because there is no supported codec.", GetApplication()->GetVHostAppName().CStr(), GetName().CStr());
		return false;
	}

	// Create default playlist
	auto default_playlist_info = GetDefaultPlaylistInfo();
	OV_ASSERT2(default_playlist_info != nullptr);

	auto default_playlist = Stream::GetPlaylist(default_playlist_info->file_name);
	if (default_playlist == nullptr)
	{
		auto playlist = std::make_shared<info::Playlist>(default_playlist_info->name, default_playlist_info->file_name, true);
		auto rendition = std::make_shared<info::Rendition>("default", first_video_track ? first_video_track->GetVariantName() : "", first_audio_track ? first_audio_track->GetVariantName() : "");

		playlist->AddRendition(rendition);
		playlist->EnableTsPackaging(true);
		AddPlaylist(playlist);
	}

	return true;
}

void HlsStream::BufferMediaPacketUntilReadyToPlay(const std::shared_ptr<MediaPacket> &media_packet)
{
	if (_initial_media_packet_buffer.Size() >= MAX_INITIAL_MEDIA_PACKET_BUFFER_SIZE)
	{
		// Drop the oldest packet, for OOM protection
		_initial_media_packet_buffer.Dequeue(0);
	}

	_initial_media_packet_buffer.Enqueue(media_packet);
}

bool HlsStream::SendBufferedPackets()
{
	logtt("SendBufferedPackets - BufferSize (%zu)", _initial_media_packet_buffer.Size());
	while (_initial_media_packet_buffer.IsEmpty() == false)
	{
		auto buffered_media_packet = _initial_media_packet_buffer.Dequeue();
		if (buffered_media_packet.has_value() == false)
		{
			continue;
		}

		auto media_packet = buffered_media_packet.value();
		if (media_packet->GetMediaType() == cmn::MediaType::Data)
		{
			SendDataFrame(media_packet);
		}
		else
		{
			AppendMediaPacket(media_packet);
		}
	}

	return true;
}

bool HlsStream::AppendMediaPacket(const std::shared_ptr<MediaPacket> &media_packet)
{
	std::shared_lock<std::shared_mutex> lock(_ts_packetizers_guard);
	auto& packetizers = _track_packetizers[media_packet->GetTrackId()];
	for (auto& packetizer : packetizers)
	{
		packetizer->AppendFrame(media_packet);
	}

	return true;
}

void HlsStream::SendVideoFrame(const std::shared_ptr<MediaPacket> &media_packet)
{
	// If the stream is concluded, it will not be processed.
	if (IsConcluded() == true)
	{
		return;
	}

	if (GetState() == State::CREATED)
	{
		BufferMediaPacketUntilReadyToPlay(media_packet);
		return;
	}

	if (_initial_media_packet_buffer.IsEmpty() == false)
	{
		SendBufferedPackets();
	}

	AppendMediaPacket(media_packet);
}

void HlsStream::SendAudioFrame(const std::shared_ptr<MediaPacket> &media_packet)
{
	// If the stream is concluded, it will not be processed.
	if (IsConcluded() == true)
	{
		return;
	}

	if (GetState() == State::CREATED)
	{
		BufferMediaPacketUntilReadyToPlay(media_packet);
		return;
	}

	if (_initial_media_packet_buffer.IsEmpty() == false)
	{
		SendBufferedPackets();
	}

	AppendMediaPacket(media_packet);
}

void HlsStream::SendDataFrame(const std::shared_ptr<MediaPacket> &media_packet)
{
	if (GetState() == State::CREATED)
	{
		BufferMediaPacketUntilReadyToPlay(media_packet);
		return;
	}

	if (_initial_media_packet_buffer.IsEmpty() == false)
	{
		SendBufferedPackets();
	}

	auto data_track = GetTrack(media_packet->GetTrackId());
	if (data_track == nullptr)
	{
		logtw("Could not find track. id: %d", media_packet->GetTrackId());
		return;
	}

	if (media_packet->GetBitstreamFormat() == cmn::BitstreamFormat::CUE)
	{
		auto timestamp = static_cast<double>(media_packet->GetDts()) / data_track->GetTimeBase().GetTimescale() * mpegts::TIMEBASE_DBL;
		auto timestamp_ms = static_cast<double>(media_packet->GetDts()) / data_track->GetTimeBase().GetTimescale() * 1000.0;
		auto data = media_packet->GetData() != nullptr ? media_packet->GetData()->Clone() : nullptr;

		auto marker = Marker::CreateMarker(media_packet->GetBitstreamFormat(), timestamp, timestamp_ms, data);
		if (marker == nullptr)
		{
			logte("(%s/%s) Failed to create the marker", GetApplication()->GetVHostAppName().CStr(), GetName().CStr());
			return;
		}

		// Insert marker to all packagers
		for (auto &it : _ts_packagers)
		{
			auto packager = it.second;
			auto result = packager->InsertMarker(marker);
			if (result == false)
			{
				logte("Failed to insert marker (timestamp: %" PRIu64 ", tag: %s)", marker->GetTimestamp(), marker->GetTag().CStr());
			}
		}

		return;
	}
	// vtt
	else if (media_packet->GetBitstreamFormat() == cmn::BitstreamFormat::WebVTT)
	{
		// Convert DataFrame to WebVTT
		auto webvtt_frame = WebVTTFrame::Parse(media_packet->GetData());
		if (webvtt_frame == nullptr)
		{
			logte("Failed to parse WebVTT frame from data packet (track_id: %d, dts: %" PRId64 ")", media_packet->GetTrackId(), media_packet->GetDts());
			return;
		}

		// Get Packager
		auto packager = GetVttPackager(data_track->GetId());
		if (packager == nullptr)
		{
			logte("Could not find WebVTT packager for label: %s", webvtt_frame->GetLabel().CStr());
			return;
		}
		
		packager->AddFrame(webvtt_frame);
		return;
	}

	AppendMediaPacket(media_packet);
}

void HlsStream::OnEvent(const std::shared_ptr<MediaEvent> &event)
{
	if (event == nullptr)
	{
		return;
	}

	switch(event->GetCommandType())
	{
		case EventCommand::Type::ConcludeLive:
		{
			auto [result, message] = ConcludeLive();
			if (result == true)
			{
				logti("HlsStream(%s/%s) - Live has concluded.", GetApplication()->GetVHostAppName().CStr(), GetName().CStr());
			}
			else
			{
				logte("HlsStream(%s/%s) - Failed to conclude live.(%s)", GetApplication()->GetVHostAppName().CStr(), GetName().CStr(), message.CStr());
			}
			break;
		}
		default:
			break;
	}
}

std::tuple<bool, ov::String> HlsStream::ConcludeLive()
{
	std::unique_lock<std::shared_mutex> lock(_concluded_lock);
	if (_concluded)
	{
		return {false, "Already concluded"};
	}

	_concluded = true;

	// Flush all packagers
	for (auto &it : _ts_packagers)
	{
		auto packager = it.second;
		packager->Flush();
	}

	// Append #EXT-X-ENDLIST all chunklists
	for (auto &it : _media_playlists)
	{
		auto playlist = it.second;
		playlist->SetEndList();
	}

	return {true, ""};
}

bool HlsStream::IsConcluded() const
{
	std::shared_lock<std::shared_mutex> lock(_concluded_lock);
	return _concluded;
}

bool HlsStream::CheckIfAllPlaylistReady()
{
	if (_ready_to_play == true)
	{
		return true;
	}

	std::shared_lock<std::shared_mutex> lock(_media_playlists_guard);

	int min_segments_for_ready = std::min(_ts_config.GetSegmentCount(), 2);

	for (const auto &[variant_name, playlist] : _media_playlists)
	{
		if (playlist == nullptr)
		{
			continue;
		}

		if (playlist->GetSegmentCount() < static_cast<std::size_t>(min_segments_for_ready))
		{
			return false;
		}
	}

	logti("HLS Stream(%s/%s) - All playlists are ready to play", GetApplication()->GetVHostAppName().CStr(), GetName().CStr());

	auto alert = MonitorInstance->GetAlert();
	auto stream_metrics = StreamMetrics(*std::static_pointer_cast<info::Stream>(pub::Stream::GetSharedPtr()));

	alert->SendStreamMessage(mon::alrt::Message::Code::EGRESS_HLS_READY, stream_metrics);

	DumpMasterPlaylistOfAllItems();

	return true;
}

void HlsStream::OnSegmentCreated(const ov::String &packager_id, const std::shared_ptr<base::modules::Segment> &segment)
{
	if (CheckIfAllPlaylistReady() == true)
	{
		_ready_to_play = true;
	}

	auto playlist = GetMediaPlaylist(packager_id);
	if (playlist == nullptr)
	{
		logte("Failed to find the media playlist");
		return;
	}

	segment->SetUrl(GetSegmentName(packager_id, segment->GetNumber()));

	if (playlist->GetWallclockOffset() == INT64_MIN)
	{
		auto first_segment_timestamp_ms = (segment->GetStartTimestamp() * segment->GetTimebaseSeconds()) * 1000.0;

		auto wallclock_offset_ms = static_cast<int64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(GetInputStreamPublishedTime().time_since_epoch()).count() - first_segment_timestamp_ms);

		OV_ASSERT(wallclock_offset_ms != INT64_MIN, "Failed to calculate wallclock offset");

		playlist->SetWallclockOffset(wallclock_offset_ms);
	}

	playlist->OnSegmentCreated(segment);

	logtt("Playlist : %s", playlist->ToString(false).CStr());

	DumpSegmentOfAllItems(packager_id, segment->GetNumber());

	if (packager_id == _vtt_reference_packager_id)
	{
		std::shared_lock<std::shared_mutex> lock(_vtt_packagers_lock);
		auto vtt_packagers = _vtt_packagers;
		lock.unlock();

		double segment_start_timestamp = static_cast<double>(segment->GetStartTimestamp()) * segment->GetTimebaseSeconds() * 1000.0;
		double segment_duration_ms = segment->GetDurationMs();

		// Create VTT segments 
		for (auto &it : vtt_packagers)
		{
			auto track_id = it.first;
			auto vtt_packager = it.second;
			
			if (vtt_packager->MakeSegment(segment->GetNumber(), segment_start_timestamp, segment_duration_ms) == false)
			{
				logte("Failed to create VTT segment for segment number: %" PRId64, segment->GetNumber());
				break;
			}

			auto vtt_segment = vtt_packager->GetSegment(segment->GetNumber());
			if (vtt_segment == nullptr)
			{
				logte("Failed to get VTT segment for segment number: %" PRId64,	segment->GetNumber());
				break;
			}

			if (segment->HasMarker() == true)
			{
				vtt_segment->SetMarkers(segment->GetMarkers());
			}

			auto variant_name = MakeVttVariantName(track_id);
			if (variant_name.IsEmpty() == true)
			{
				logte("Failed to make VTT variant name for track id: %d", track_id);
				break;
			}
			
			OnSegmentCreated(variant_name, vtt_segment);
		}
	}
}

void HlsStream::OnSegmentDeleted(const ov::String &packager_id, const std::shared_ptr<base::modules::Segment> &segment)
{
	auto playlist = GetMediaPlaylist(packager_id);
	if (playlist == nullptr)
	{
		logte("Failed to find the media playlist");
		return;
	}

	playlist->OnSegmentDeleted(segment);

	if (packager_id == _vtt_reference_packager_id)
	{
		std::shared_lock<std::shared_mutex> lock(_vtt_packagers_lock);
		auto vtt_packagers = _vtt_packagers;
		lock.unlock();

		// Delete VTT segments 
		for (auto &it : vtt_packagers)
		{
			auto track_id = it.first;
			auto vtt_packager = it.second;

			auto variant_name = MakeVttVariantName(track_id);
			if (variant_name.IsEmpty() == true)
			{
				logte("Failed to make VTT variant name for track id: %d", track_id);
				break;
			}

			auto vtt_segment = vtt_packager->GetSegment(segment->GetNumber());
			if (vtt_segment == nullptr)
			{
				logte("Failed to get VTT segment for segment number: %" PRId64, segment->GetNumber());
				break;
			}

			OnSegmentDeleted(variant_name, vtt_segment);

			vtt_packager->DeleteSegment(segment->GetNumber());
		}
	}
}

bool HlsStream::CreatePackagers()
{
	// VTT
	auto &tracks = GetTracks();
	for (const auto &[id, track] : tracks)
	{
		if (track->GetCodecId() != cmn::MediaCodecId::WebVTT)
		{
			continue;
		}

		if (AddVttPackager(track) == false)
		{
			logte("Failed to create VTT packager for track id: %d", track->GetId());
			continue;
		}
	}

	for (auto &it : GetPlaylists())
	{
		auto playlist = it.second;
		if (playlist == nullptr)
		{
			continue;
		}

		if (playlist->IsTsPackagingEnabled() == false)
		{
			continue;
		}

		HlsMasterPlaylist::Config master_playlist_config;

		auto master_playlist = std::make_shared<HlsMasterPlaylist>(playlist->GetFileName(), master_playlist_config);
		master_playlist->SetDefaultOption(_default_option_rewind);

		// VTT
		if (playlist->IsSubtitlesEnabled())
		{
			std::shared_lock<std::shared_mutex> lock(_media_playlists_guard);
			for (const auto &[variant_name, media_playlist] : _media_playlists)
			{
				if (media_playlist->HasSubtitle() == true)
				{
					master_playlist->AddVttPlaylist(media_playlist);
				}
			}
		}

		{
			std::lock_guard<std::shared_mutex> lock(_master_playlists_guard);
			_master_playlists.emplace(master_playlist->GetFileName(), master_playlist);
			logti("HLS Master Playlist has been created : %s", master_playlist->GetFileName().CStr());
		}

		for (const auto &rendition : playlist->GetRenditionList())
		{
			// MediaPlaylist
			// Packager
			// Packetizer

			auto video_variant_name = rendition->GetVideoVariantName();
			auto audio_variant_name = rendition->GetAudioVariantName();
			auto video_index_hint = rendition->GetVideoIndexHint();
			auto audio_index_hint = rendition->GetAudioIndexHint();

			if (video_variant_name.IsEmpty() == false && GetMediaTrackGroup(video_variant_name) == nullptr)
			{
				logtw("HLS Stream(%s/%s) - The variant name video(%s) in the rendition %s is not found in the track list, it will be ignored", GetApplication()->GetVHostAppName().CStr(), GetName().CStr(), video_variant_name.CStr(), playlist->GetFileName().CStr());
				video_variant_name.Clear();
			}

			if (audio_variant_name.IsEmpty() == false && GetMediaTrackGroup(audio_variant_name) == nullptr)
			{
				logtw("HLS Stream(%s/%s) - The variant name audio(%s) in the rendition %s is not found in the track list, it will be ignored", GetApplication()->GetVHostAppName().CStr(), GetName().CStr(), audio_variant_name.CStr(), playlist->GetFileName().CStr());
				audio_variant_name.Clear();
			}

			// Check if there is a track with the variant name
			if (video_variant_name.IsEmpty() == true && audio_variant_name.IsEmpty() == true)
			{
				logtw("HLS Stream(%s/%s) - Invalid rendition %s. The variant name video(%s) audio(%s) is not found in the track list", GetApplication()->GetVHostAppName().CStr(), GetName().CStr(), playlist->GetFileName().CStr(), video_variant_name.CStr(), audio_variant_name.CStr());
				continue;
			}

			// Check if the rendition has supported codec
			if ((GetFirstTrackByVariant(video_variant_name) != nullptr && IsSupportedCodec(GetFirstTrackByVariant(video_variant_name)->GetCodecId()) == false) ||
				(GetFirstTrackByVariant(audio_variant_name) != nullptr && IsSupportedCodec(GetFirstTrackByVariant(audio_variant_name)->GetCodecId()) == false))
			{
				logtw("HLS Stream(%s/%s) - Exclude the rendition(%s) from the %s playlist due to unsupported codec", GetApplication()->GetVHostAppName().CStr(), GetName().CStr(), rendition->GetName().CStr(), playlist->GetFileName().CStr());
				continue;
			}

			auto variant_name = GetVariantName(video_variant_name, video_index_hint, audio_variant_name, audio_index_hint);
			auto media_playlist = GetMediaPlaylist(variant_name);
			if (media_playlist != nullptr)
			{
				// Variant is already created, we can use the existing media playlist
				master_playlist->AddMediaPlaylist(media_playlist);
				continue;
			}

			// Already created
			if (GetTSPacketizer(variant_name) != nullptr)
			{
				continue;
			}

			//////////////////////////////////
			// Create Media Playlist
			//////////////////////////////////
			HlsMediaPlaylist::HlsMediaPlaylistConfig media_playlist_config;
			media_playlist_config.segment_count = _ts_config.GetSegmentCount();
			media_playlist_config.target_duration = _ts_config.GetSegmentDuration();
			media_playlist_config.event_playlist_type = _ts_config.GetDvr().IsEnabled() && _ts_config.GetDvr().IsEventPlaylistType();

			auto media_playlist_name = GetMediaPlaylistName(variant_name);
			media_playlist = std::make_shared<HlsMediaPlaylist>(variant_name, media_playlist_name, media_playlist_config);
			if (media_playlist == nullptr)
			{
				logte("Failed to create media playlist");
				return false;
			}

			{
				std::lock_guard<std::shared_mutex> lock(_media_playlists_guard);
				_media_playlists.emplace(variant_name, media_playlist);
			}

			//////////////////////////////////
			// Create Packager
			//////////////////////////////////
			mpegts::Packager::Config packager_config;
			packager_config.target_duration_ms = _ts_config.GetSegmentDuration() * 1000;
			packager_config.max_segment_count = _ts_config.GetSegmentCount();

			auto dvr_config = _ts_config.GetDvr();
			if (dvr_config.IsEnabled())
			{
				packager_config.dvr_window_ms = dvr_config.GetMaxDuration() * 1000;
				packager_config.dvr_storage_path = dvr_config.GetTempStoragePath();
			}

			packager_config.stream_id_meta = ov::String::FormatString("%s_%s", GetApplicationName(), GetName().CStr());

			auto packager = std::make_shared<mpegts::Packager>(variant_name, packager_config);
			if (packager == nullptr)
			{
				logte("Failed to create packager");
				return false;
			}

			{
				std::lock_guard<std::shared_mutex> lock(_ts_packagers_guard);
				_ts_packagers.emplace(variant_name, packager);
			}

			{
				std::lock_guard<std::shared_mutex> lock(_storage_map_guard);
				_storage_map.emplace(variant_name, packager);
			}

			packager->AddSink(mpegts::PackagerSink::GetSharedPtr());

			//////////////////////////////////
			// Create Packetizer
			//////////////////////////////////
			auto packetizer = std::make_shared<mpegts::Packetizer>();
			if (packetizer == nullptr)
			{
				logte("Failed to create packager");
				return false;
			}

			{
				std::lock_guard<std::shared_mutex> lock(_ts_packetizers_guard);
				_ts_packetizers.emplace(variant_name, packetizer);
			}

			packetizer->AddSink(packager);

			if (video_variant_name.IsEmpty() == false)
			{
				auto video_track_group = GetMediaTrackGroup(video_variant_name);

				if (video_index_hint != -1)
				{
					auto track = video_track_group->GetTrack(video_index_hint);
					if (track == nullptr)
					{
						logtw("HLS Stream(%s/%s) - The video track index %d in the rendition %s is not found in the track list, it will be ignored", GetApplication()->GetVHostAppName().CStr(), GetName().CStr(), video_index_hint, playlist->GetFileName().CStr());
					}
					else
					{
						packetizer->AddTrack(track);

						// Add to track packetizers
						{
							std::lock_guard<std::shared_mutex> lock(_ts_packetizers_guard);
							_track_packetizers[track->GetId()].emplace_back(packetizer);
						}

						media_playlist->AddMediaTrackInfo(track);
					}
				}
				else
				{
					for (auto track : video_track_group->GetTracks())
					{
						packetizer->AddTrack(track);

						// Add to track packetizers
						{
							std::lock_guard<std::shared_mutex> lock(_ts_packetizers_guard);
							_track_packetizers[track->GetId()].emplace_back(packetizer);
						}

						media_playlist->AddMediaTrackInfo(track);

						// EXT-X-MEDIA is supported in EXT-X-VERSION 4 or higher
						// Now we support EXT-X-VERSION 3
						// Therefore, we don't support multiple video tracks now
						break;
					}
				}
			}

			if (audio_variant_name.IsEmpty() == false)
			{
				auto audio_track_group = GetMediaTrackGroup(audio_variant_name);
				if (audio_index_hint != -1)
				{
					auto track = audio_track_group->GetTrack(audio_index_hint);
					if (track == nullptr)
					{
						logtw("HLS Stream(%s/%s) - The audio track index %d in the rendition %s is not found in the track list, it will be ignored", GetApplication()->GetVHostAppName().CStr(), GetName().CStr(), audio_index_hint, playlist->GetFileName().CStr());
					}
					else
					{
						packetizer->AddTrack(track);

						// Add to track packetizers
						{
							std::lock_guard<std::shared_mutex> lock(_ts_packetizers_guard);
							_track_packetizers[track->GetId()].emplace_back(packetizer);
						}

						media_playlist->AddMediaTrackInfo(track);
					}
				}
				else
				{
					for (auto track : audio_track_group->GetTracks())
					{
						packetizer->AddTrack(track);

						// Add to track packetizers
						{
							std::lock_guard<std::shared_mutex> lock(_ts_packetizers_guard);
							_track_packetizers[track->GetId()].emplace_back(packetizer);
						}

						media_playlist->AddMediaTrackInfo(track);
					}
				}
			}

			auto data_track = GetFirstTrackByType(cmn::MediaType::Data);
			if (data_track != nullptr)
			{
				packetizer->AddTrack(data_track);

				// Add to track packetizers
				{
					std::lock_guard<std::shared_mutex> lock(_ts_packetizers_guard);
					_track_packetizers[data_track->GetId()].emplace_back(packetizer);
				}
			}

			master_playlist->AddMediaPlaylist(media_playlist);
			packetizer->Start();

			if (_vtt_reference_packager_id.IsEmpty() == true)
			{
				// First created packager is used as the reference packager for VTT
				_vtt_reference_packager_id = variant_name;
			}
		} // for (const auto &rendition : playlist->GetRenditionList())
	} // for (auto &it : GetPlaylists())

	return true;
}

ov::String HlsStream::GetVariantName(const ov::String &video_variant_name, int video_index, const ov::String &audio_variant_name, int audio_index) const
{
	auto variant_name = ov::String::FormatString("%s%d_%s%d", video_variant_name.CStr(), video_index, audio_variant_name.CStr(), audio_index);
	return ov::Converter::ToString(variant_name.Hash());
}

ov::String HlsStream::MakeVttVariantName(const int32_t &track_id) const
{
	auto track = GetTrack(track_id);
	if (track == nullptr)
	{
		logte("Could not find track for id: %d", track_id);
		return "";
	}

	return GetVariantName(track->GetPublicName(), track->GetId(), track->GetLanguage(), -1);
}

std::shared_ptr<HlsMasterPlaylist> HlsStream::GetMasterPlaylist(const ov::String &playlist_name)
{
	std::shared_lock<std::shared_mutex> lock(_master_playlists_guard);

	auto it = _master_playlists.find(playlist_name);
	if (it == _master_playlists.end())
	{
		return nullptr;
	}

	return it->second;
}

ov::String HlsStream::GetMediaPlaylistName(const ov::String &variant_name) const
{
	return ov::String::FormatString("medialist_%s_hls.m3u8", variant_name.CStr());
}

ov::String HlsStream::GetSegmentName(const ov::String &variant_name, uint32_t number) const
{
	auto storage = GetStorage(variant_name);
	if (storage == nullptr)
	{
		return "";
	}

	ov::String extension = storage->GetContainerExtension();
	return ov::String::FormatString("seg_%s_%u_hls.%s", variant_name.CStr(), number, extension.CStr());
}

std::shared_ptr<mpegts::Packetizer> HlsStream::GetTSPacketizer(const ov::String &variant_name)
{
	std::shared_lock<std::shared_mutex> lock(_ts_packetizers_guard);

	auto it = _ts_packetizers.find(variant_name);
	if (it == _ts_packetizers.end())
	{
		return nullptr;
	}

	return it->second;
}

std::shared_ptr<mpegts::Packager> HlsStream::GetTSPackager(const ov::String &variant_name)
{
	std::shared_lock<std::shared_mutex> lock(_ts_packagers_guard);

	auto it = _ts_packagers.find(variant_name);
	if (it == _ts_packagers.end())
	{
		return nullptr;
	}

	return it->second;
}

std::shared_ptr<webvtt::Packager> HlsStream::GetVttPackager(const int32_t &track_id) const
{
	std::shared_lock<std::shared_mutex> lock(_vtt_packagers_lock);

	auto it = _vtt_packagers.find(track_id);
	if (it == _vtt_packagers.end())
	{
		return nullptr;
	}

	return it->second;
}

std::shared_ptr<base::modules::SegmentStorage> HlsStream::GetStorage(const ov::String &variant_name) const
{
	std::shared_lock<std::shared_mutex> lock(_storage_map_guard);

	auto it = _storage_map.find(variant_name);
	if (it == _storage_map.end())
	{
		return nullptr;
	}

	return it->second;
}

std::shared_ptr<HlsMediaPlaylist> HlsStream::GetMediaPlaylist(const ov::String &variant_name)
{
	std::shared_lock<std::shared_mutex> lock(_media_playlists_guard);

	auto it = _media_playlists.find(variant_name);
	if (it == _media_playlists.end())
	{
		return nullptr;
	}

	return it->second;
}

std::tuple<HlsStream::RequestResult, std::shared_ptr<const ov::Data>> HlsStream::GetMasterPlaylistData(const ov::String &playlist_name, bool rewind)
{
	auto playlist = GetMasterPlaylist(playlist_name);
	if (playlist == nullptr)
	{
		return {RequestResult::NotFound, nullptr};
	}

	auto data = playlist->ToString(rewind).ToData(false);
	if (data == nullptr)
	{
		return std::make_tuple(RequestResult::UnknownError, nullptr);
	}

	return std::make_tuple(RequestResult::Success, data);
}

std::tuple<HlsStream::RequestResult, std::shared_ptr<const ov::Data>> HlsStream::GetMediaPlaylistData(const ov::String &variant_name, bool rewind)
{
	auto playlist = GetMediaPlaylist(variant_name);
	if (playlist == nullptr)
	{
		return std::make_tuple(RequestResult::NotFound, nullptr);
	}

	auto data = playlist->ToString(rewind).ToData(false);
	if (data == nullptr)
	{
		return std::make_tuple(RequestResult::UnknownError, nullptr);
	}

	return std::make_tuple(RequestResult::Success, data);
}

std::tuple<HlsStream::RequestResult, std::shared_ptr<const ov::Data>> HlsStream::GetSegmentData(const ov::String &variant_name, uint32_t number)
{
	auto storage = GetStorage(variant_name);
	if (storage == nullptr)
	{
		return std::make_tuple(RequestResult::NotFound, nullptr);
	}

	auto segment = storage->GetSegment(number);
	if (segment == nullptr)
	{
		return std::make_tuple(RequestResult::NotFound, nullptr);
	}

	return std::make_tuple(RequestResult::Success, segment->GetData());
}

void HlsStream::InitializeAllDumps()
{
	auto dump_configs = _ts_config.GetDumps().GetDumps();
	// Select the dump setting for this stream.
	std::lock_guard<std::shared_mutex> lock(_dumps_lock);
	for (auto dump : dump_configs)
	{
		if (dump.IsEnabled() == false)
		{
			continue;
		}

		// check if dump.TargetStreamName is same as this stream name
		auto match_result = dump.GetTargetStreamNameRegex().Matches(GetName().CStr());
		if (match_result.IsMatched())
		{
			auto output_path = dump.GetOutputPath();
			dump.ConfigureOutputPath(output_path, GetApplication()->GetVHostAppName().GetVHostName(), GetApplication()->GetVHostAppName().GetAppName(), GetName());

			auto dump_item = std::make_shared<mdl::Dump>();
			dump_item->SetId(dump.GetId());
			dump_item->SetOutputPath(output_path);
			dump_item->SetPlaylists(dump.GetPlaylists());
			dump_item->SetEnabled(true);

			_dumps.emplace(dump_item->GetId(), dump_item);
		}
	}
}

void HlsStream::DumpMasterPlaylistOfAllItems()
{
	std::shared_lock<std::shared_mutex> lock(_dumps_lock);
	for (auto &it : _dumps)
	{
		auto dump = it.second;
		if (dump->IsEnabled() == false)
		{
			continue;
		}

		if (DumpMasterPlaylist(dump) == false)
		{
			// Event if the dump fails, it will not be deleted
			//dump->SetEnabled(false);
		}
	}
}

bool HlsStream::DumpMasterPlaylist(const std::shared_ptr<mdl::Dump> &dump)
{
	if (dump->IsEnabled() == false)
	{
		return false;
	}

	for (const auto &playlist_name : dump->GetPlaylists())
	{
		auto file_name_without_ext = playlist_name.Substring(0, playlist_name.IndexOfRev('.'));
		auto [result, data] = GetMasterPlaylistData(file_name_without_ext, false);
		if (result == RequestResult::NotFound)
		{
			logtw("Could not get master playlist(%s) for dump", playlist_name.CStr());
			return false;
		}

		if (!DumpData(dump, playlist_name, data))
		{
			logtw("Could not dump master playlist(%s)", playlist_name.CStr());
			return false;
		}
	}

	return true;
}

void HlsStream::DumpSegmentOfAllItems(const ov::String &packager_id, const uint32_t &segment_number)
{
	std::shared_lock<std::shared_mutex> lock(_dumps_lock);
	for (auto &it : _dumps)
	{
		auto dump = it.second;
		if (dump->IsEnabled() == false)
		{
			continue;
		}

		if (DumpSegment(dump, packager_id, segment_number) == false)
		{
			dump->SetEnabled(false);
			continue;
		}
	}
}

bool HlsStream::DumpSegment(const std::shared_ptr<mdl::Dump> &dump, const ov::String &packager_id, const uint32_t &segment_number)
{
	// Write segment data
	auto storage = GetStorage(packager_id);  // packager_id is variant_name
	if (!storage)
	{
		logte("HlsStream(%s/%s) - Could not find packager for variant %s during dumping.",
			  GetApplication()->GetVHostAppName().CStr(), GetName().CStr(), packager_id.CStr());
		return false;
	}

	auto segment = storage->GetSegment(segment_number);
	if (!segment)
	{
		logte("HlsStream(%s/%s) - Could not get segment for dumping variant %s, segment %u",
			  GetApplication()->GetVHostAppName().CStr(), GetName().CStr(),
			  packager_id.CStr(), segment_number);
		return false;
	}

	auto segment_data = segment->GetData();
	if (!segment_data)
	{
		logte("HlsStream(%s/%s) - Could not get segment data for dumping variant %s, segment %u",
			  GetApplication()->GetVHostAppName().CStr(), GetName().CStr(),
			  packager_id.CStr(), segment_number);
		return false;
	}

	auto segment_filename = GetSegmentName(packager_id, segment_number);
	if (!DumpData(dump, segment_filename, segment_data, false))
	{
		logte("HlsStream(%s/%s) - Dump '%s' failed to dump segment: %s",
			  GetApplication()->GetVHostAppName().CStr(), GetName().CStr(), dump->GetId().CStr(), segment_filename.CStr());
		return false;
	}

	// Append to media playlist data
	auto playlist = GetMediaPlaylist(packager_id);
	if (!playlist)
	{
		logte("HlsStream(%s/%s) - Could not find playlist for variant %s during dumping.",
			  GetApplication()->GetVHostAppName().CStr(), GetName().CStr(), packager_id.CStr());
		return false;
	}

	std::shared_ptr<ov::Data> playlist_data;
	if (segment_number == 0)
	{
		playlist_data = playlist->ToString(false).ToData(false);
		if (!playlist_data)
		{
			logte("HlsStream(%s/%s) - Could not get playlist data for variant %s during dumping.",
				  GetApplication()->GetVHostAppName().CStr(), GetName().CStr(), packager_id.CStr());
			return false;
		}
	}
	else
	{
		auto playlist_latest_segment = playlist->GetLatestSegment();
		if (!playlist_latest_segment)
		{
			logte("HlsStream(%s/%s) - Could not find latest segment for variant %s during dumping.",
				GetApplication()->GetVHostAppName().CStr(), GetName().CStr(), packager_id.CStr());
			return false;
		}
		playlist_data = playlist->MakeSegmentString(playlist_latest_segment).ToData(false);
	}

	if (DumpData(dump, playlist->GetPlaylistFileName(), playlist_data, true) == false)
	{
		logte("HlsStream(%s/%s) - Dump '%s' failed to dump media playlist: %s",
			  GetApplication()->GetVHostAppName().CStr(), GetName().CStr(), dump->GetId().CStr(), playlist->GetPlaylistFileName().CStr());
		return false;
	}

	return true;
}

bool HlsStream::DumpData(const std::shared_ptr<mdl::Dump> &dump, const ov::String &file_name, const std::shared_ptr<const ov::Data> &data, bool append)
{
	return dump->DumpData(file_name, data, append);
}

void HlsStream::StopDumps()
{
	std::lock_guard<std::shared_mutex> dumper_lock(_dumps_lock);
	for (auto &[dump_id, dump] : _dumps)
	{
		StopDump(dump);
	}
	_dumps.clear();
}

void HlsStream::StopDump(const std::shared_ptr<mdl::Dump> &dump)
{
	if (!dump)
	{
		return;
	}

	logti("HlsStream(%s/%s) - Stopping dump '%s'.", GetApplication()->GetVHostAppName().CStr(), GetName().CStr(), dump->GetId().CStr());

	// Dump final master playlists
	DumpMasterPlaylist(dump);

	dump->CompleteDump();
}

bool HlsStream::AddVttPackager(const std::shared_ptr<const MediaTrack> &track)
{
	if (track->GetMediaType() != cmn::MediaType::Subtitle || track->GetCodecId() != cmn::MediaCodecId::WebVTT)
	{
		logte("Track is not WebVTT. id: %d, media_type: %s, codec_id: %s", track->GetId(), cmn::GetMediaTypeString(track->GetMediaType()), cmn::GetCodecIdString(track->GetCodecId()));
		return false;
	}

	// packager
	auto packager = std::make_shared<webvtt::Packager>(track);
	{
		std::lock_guard<std::shared_mutex> lock(_vtt_packagers_lock);
		_vtt_packagers[track->GetId()] = packager;
	}

	auto variant_name = MakeVttVariantName(track->GetId());

	// storage
	{
		// The VTT packager also functions as storage.
		std::lock_guard<std::shared_mutex> storage_lock(_storage_map_guard);
		_storage_map.emplace(variant_name, packager);
	}

	// media playlist
	HlsMediaPlaylist::HlsMediaPlaylistConfig media_playlist_config;
	media_playlist_config.segment_count = _ts_config.GetSegmentCount();
	media_playlist_config.target_duration = _ts_config.GetSegmentDuration();
	media_playlist_config.event_playlist_type = _ts_config.GetDvr().IsEventPlaylistType();

	auto media_playlist_name = GetMediaPlaylistName(variant_name);
	auto media_playlist = std::make_shared<HlsMediaPlaylist>(variant_name, media_playlist_name, media_playlist_config);
	if (media_playlist == nullptr)
	{
		logte("Failed to create media playlist");
		return false;
	}

	media_playlist->AddMediaTrackInfo(track);

	{
		std::lock_guard<std::shared_mutex> lock(_media_playlists_guard);
		_media_playlists.emplace(variant_name, media_playlist);
	}


	return true;
}