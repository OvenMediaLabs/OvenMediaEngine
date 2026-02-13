//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2024 AirenSoft. All rights reserved.
//
//==============================================================================
#include "srt_stream.h"

#include <orchestrator/orchestrator.h>

#include "base/publisher/application.h"
#include "base/publisher/stream.h"
#include "srt_playlist.h"
#include "srt_private.h"
#include "srt_session.h"

#define OV_LOG_PREFIX_FORMAT "[%s/%s(%u)] "
#define OV_LOG_PREFIX_VALUE GetApplicationName(), GetName().CStr(), GetId()

namespace pub
{
	std::shared_ptr<SrtStream> SrtStream::Create(const std::shared_ptr<Application> application,
												 const info::Stream &info,
												 uint32_t worker_count)
	{
		auto stream = std::make_shared<SrtStream>(application, info, worker_count);
		return stream;
	}

	SrtStream::SrtStream(const std::shared_ptr<Application> application,
						 const info::Stream &info,
						 uint32_t worker_count)
		: Stream(application, info),
		  _worker_count(worker_count)
	{
		logat("SrtStream has been started");
	}

	SrtStream::~SrtStream()
	{
		logat("SrtStream has been terminated finally");
	}

	std::shared_ptr<const Stream::DefaultPlaylistInfo> SrtStream::GetDefaultPlaylistInfo() const
	{
		static auto info = std::make_shared<Stream::DefaultPlaylistInfo>(
			"srt_default",
			"playlist",
			"playlist");

		return info;
	}

	std::shared_ptr<const info::Playlist> SrtStream::PrepareDefaultPlaylist()
	{
		auto default_playlist_info = GetDefaultPlaylistInfo();
		OV_ASSERT2(default_playlist_info != nullptr);

		auto playlist = Stream::GetPlaylist(default_playlist_info->file_name);

		if (playlist != nullptr)
		{
			// The playlist is already created - user has created it manually
			return playlist;
		}

		// Pick the first track of each media type
		std::shared_ptr<MediaTrack> first_video_track = nullptr;
		std::shared_ptr<MediaTrack> first_audio_track = nullptr;

		for (const auto &[id, track] : GetSupportedTracks(GetTracks()))
		{
			const auto media_type = track->GetMediaType();

			switch (media_type)
			{
				case cmn::MediaType::Video:
					first_video_track = (first_video_track == nullptr) ? track : first_video_track;
					break;

				case cmn::MediaType::Audio:
					first_audio_track = (first_audio_track == nullptr) ? track : first_audio_track;
					break;

				default:
					logat("SrtStream - Ignore unsupported media type: %s", GetMediaTypeString(track->GetMediaType()));
					break;
			}
		}

		if ((first_video_track == nullptr) && (first_audio_track == nullptr))
		{
			logaw("There is no track to create SRT stream");
			return nullptr;
		}

		auto new_playlist = std::make_shared<info::Playlist>(default_playlist_info->name, default_playlist_info->file_name, true);
		auto rendition = std::make_shared<info::Rendition>(
			"default",
			(first_video_track != nullptr) ? first_video_track->GetVariantName() : "",
			(first_audio_track != nullptr) ? first_audio_track->GetVariantName() : "");

		new_playlist->AddRendition(rendition);

		AddPlaylist(new_playlist);

		return new_playlist;
	}

	bool SrtStream::IsSupportedTrack(const std::shared_ptr<MediaTrack> &track) const
	{
		auto media_type = track->GetMediaType();

		if ((media_type == cmn::MediaType::Video) || (media_type == cmn::MediaType::Audio))
		{
			auto codec_id = track->GetCodecId();

			if (mpegts::Packetizer::IsSupportedCodec(codec_id))
			{
				return true;
			}
			else
			{
				logaw("Ignore unsupported %s codec (%s)", cmn::GetMediaTypeString(media_type), cmn::GetCodecIdString(codec_id));
			}
		}
		else if (media_type == cmn::MediaType::Data)
		{
			return true;
		}
		else
		{
			logaw("Ignore unsupported media type: %s", cmn::GetMediaTypeString(media_type));
		}

		return false;
	}

	std::shared_ptr<SrtPlaylist> SrtStream::GetSrtPlaylistInternal(const ov::String &file_name)
	{
		auto item = _srt_playlist_map_by_file_name.find(file_name);
		if (item == _srt_playlist_map_by_file_name.end())
		{
			return nullptr;
		}

		return item->second;
	}

	std::shared_ptr<SrtPlaylist> SrtStream::GetSrtPlaylist(const ov::String &file_name)
	{
		std::shared_lock lock(_srt_playlist_map_mutex);

		return GetSrtPlaylistInternal(file_name);
	}

	std::map<int32_t, std::shared_ptr<MediaTrack>> SrtStream::GetSupportedTracks(const std::map<int32_t, std::shared_ptr<MediaTrack>> &track_map) const
	{
		return ov::maputils::Filter(
			track_map,
			std::bind(&SrtStream::IsSupportedTrack, this, std::placeholders::_2));
	}

	std::vector<std::shared_ptr<MediaTrack>> SrtStream::GetSupportedTracks(const std::vector<std::shared_ptr<MediaTrack>> &tracks) const
	{
		std::vector<std::shared_ptr<MediaTrack>> supported_tracks;

		std::copy_if(
			tracks.begin(), tracks.end(),
			std::back_inserter(supported_tracks),
			std::bind(&SrtStream::IsSupportedTrack, this, std::placeholders::_1));

		return supported_tracks;
	}

	std::vector<std::shared_ptr<MediaTrack>> SrtStream::GetSupportedTracks(const std::shared_ptr<MediaTrackGroup> &group) const
	{
		return (group != nullptr) ? GetSupportedTracks(group->GetTracks()) : std::vector<std::shared_ptr<MediaTrack>>();
	}

	void SrtStream::AddSupportedTrack(const std::shared_ptr<MediaTrack> &track, std::map<ov::String, std::shared_ptr<MediaTrack>> &to)
	{
		auto media_type = track->GetMediaType();

		if ((media_type == cmn::MediaType::Video) || (media_type == cmn::MediaType::Audio))
		{
			auto codec_id = track->GetCodecId();

			if (mpegts::Packetizer::IsSupportedCodec(codec_id))
			{
				to[track->GetVariantName()] = track;
			}
			else
			{
				logai("SrtStream - Ignore unsupported %s codec (%s)",
					  cmn::GetMediaTypeString(media_type),
					  cmn::GetCodecIdString(codec_id));
			}
		}
		else if (media_type == cmn::MediaType::Data)
		{
			to[track->GetVariantName()] = track;
		}
		else
		{
			logai("SrtStream - Ignore unsupported media type: %s", cmn::GetMediaTypeString(media_type));
		}
	}

	void SrtStream::PrepareForTrack(
		const std::shared_ptr<MediaTrack> &track,
		std::map<uint32_t, std::shared_ptr<ov::Data>> &psi_data_map,
		std::map<uint32_t, std::shared_ptr<ov::Data>> &data_to_send_map)
	{
		psi_data_map[track->GetId()] = std::make_shared<ov::Data>();
		data_to_send_map[track->GetId()] = std::make_shared<ov::Data>();
	}

	bool SrtStream::Start()
	{
		std::unique_lock lock(_srt_playlist_map_mutex);

		if (GetState() != Stream::State::CREATED)
		{
			return false;
		}

		if (CreateStreamWorker(_worker_count) == false)
		{
			return false;
		}

		auto config = GetApplication()->GetConfig();
		auto srt_config = config.GetPublishers().GetSrtPublisher();

		std::map<int32_t, std::shared_ptr<MediaTrack>> data_tracks;

		PrepareDefaultPlaylist();

		data_tracks = ov::maputils::Filter(GetSupportedTracks(GetTracks()), [](int32_t track_id, const std::shared_ptr<MediaTrack> &track) {
			return track->GetMediaType() == cmn::MediaType::Data;
		});

		auto suceeded = true;

		for (const auto &[file_name, playlist] : GetPlaylists())
		{
			if ((playlist->IsDefault() == false) && (playlist->IsTsPackagingEnabled() == false))
			{
				continue;
			}

			auto &rendition_list = playlist->GetRenditionList();

			auto srt_playlist = GetSrtPlaylistInternal(file_name);

			if (srt_playlist != nullptr)
			{
				// The playlist is already created
				continue;
			}

			srt_playlist = std::make_shared<SrtPlaylist>(
				GetSharedPtrAs<info::Stream>(),
				playlist,
				GetSharedPtrAs<SrtPlaylistSink>());

			_srt_playlist_map_by_file_name[file_name] = srt_playlist;

			auto first_supported_rendition_found = false;

			for (const auto &rendition : rendition_list)
			{
				if (first_supported_rendition_found == false)
				{
					auto video_variant_name = rendition->GetVideoVariantName();
					auto audio_variant_name = rendition->GetAudioVariantName();

					auto video_media_track_group = (video_variant_name.IsEmpty() == false) ? GetMediaTrackGroup(video_variant_name) : nullptr;
					auto audio_media_track_group = (audio_variant_name.IsEmpty() == false) ? GetMediaTrackGroup(audio_variant_name) : nullptr;

					if ((video_variant_name.IsEmpty() == false) && (video_media_track_group == nullptr))
					{
						logaw("%s video is excluded from the %s rendition in %s playlist because there is no video track.",
							  video_variant_name.CStr(), rendition->GetName().CStr(), playlist->GetFileName().CStr());
						video_variant_name.Clear();
					}

					if ((audio_variant_name.IsEmpty() == false) && (audio_media_track_group == nullptr))
					{
						logaw("%s audio is excluded from the %s rendition in %s playlist because there is no audio track.",
							  audio_variant_name.CStr(), rendition->GetName().CStr(), playlist->GetFileName().CStr());
						audio_variant_name.Clear();
					}

					if (video_variant_name.IsEmpty() && audio_variant_name.IsEmpty())
					{
						logaw("Invalid rendition %s. The variant name video(%s) audio(%s) is not found in the track list",
							  rendition->GetName().CStr(), video_variant_name.CStr(), audio_variant_name.CStr());
						continue;
					}

					auto video_tracks = GetSupportedTracks(video_media_track_group);
					auto audio_tracks = GetSupportedTracks(audio_media_track_group);

					if (video_tracks.empty() && audio_tracks.empty())
					{
						logaw("Could not add variants (video: %s, audio: %s) because there is no supported codec.",
							  video_variant_name.CStr(), audio_variant_name.CStr());
						continue;
					}

					for (const auto &track : video_tracks)
					{
						_srt_playlist_map_by_track_id[track->GetId()].push_back(srt_playlist);
					}
					srt_playlist->AddTracks(video_tracks);

					for (const auto &track : audio_tracks)
					{
						_srt_playlist_map_by_track_id[track->GetId()].push_back(srt_playlist);
					}
					srt_playlist->AddTracks(audio_tracks);

					// Add data tracks to the default playlist
					for (const auto &[id, track] : data_tracks)
					{
						_srt_playlist_map_by_track_id[track->GetId()].push_back(srt_playlist);
						srt_playlist->AddTrack(track);
					}

					first_supported_rendition_found = true;

					logai("A SRT playist [%s] has been created (with variant: %s, %s)",
						  file_name.CStr(),
						  video_variant_name.CStr(),
						  audio_variant_name.CStr());
				}
				else
				{
					logaw("Rendition [%s] is ignored - SRT stream supports only one rendition per playlist", rendition->GetName().CStr());
				}
			}

			suceeded = suceeded && srt_playlist->Start();

			if (suceeded == false)
			{
				logae("Could not start the SRT playlist: %s", file_name.CStr());
				break;
			}
		}

		auto result = Stream::Start();

		if (result)
		{
			logai("The SRT stream has been started");
		}
		else
		{
			logae("Failed to start the SRT stream");
		}

		return result;
	}

	bool SrtStream::Stop()
	{
		std::unique_lock lock(_srt_playlist_map_mutex);

		if (GetState() != Stream::State::STARTED)
		{
			return false;
		}

		for (const auto &[file_name, playlist] : _srt_playlist_map_by_file_name)
		{
			playlist->Stop();
		}

		_srt_playlist_map_by_file_name.clear();
		_srt_playlist_map_by_track_id.clear();

		auto result = Stream::Stop();

		if (result)
		{
			logai("The SRT stream has been stopped");
		}
		else
		{
			logae("Failed to stop the SRT stream");
		}

		return result;
	}

	void SrtStream::EnqueuePacket(const std::shared_ptr<MediaPacket> &media_packet)
	{
		std::shared_lock lock(_srt_playlist_map_mutex);

		if (GetState() != Stream::State::STARTED)
		{
			return;
		}

		auto track_id = media_packet->GetTrackId();

		auto srt_playlists_iterator = _srt_playlist_map_by_track_id.find(track_id);

		if (srt_playlists_iterator == _srt_playlist_map_by_track_id.end())
		{
			// It may have been filtered out because it is an unsupported codec
			return;
		}

		auto &srt_playlists = srt_playlists_iterator->second;

		for (const auto &srt_playlist : srt_playlists)
		{
			srt_playlist->EnqueuePacket(media_packet);
		}
	}

	void SrtStream::SendVideoFrame(const std::shared_ptr<MediaPacket> &media_packet)
	{
		EnqueuePacket(media_packet);
	}

	void SrtStream::SendAudioFrame(const std::shared_ptr<MediaPacket> &media_packet)
	{
		EnqueuePacket(media_packet);
	}

	void SrtStream::SendDataFrame(const std::shared_ptr<MediaPacket> &media_packet)
	{
		EnqueuePacket(media_packet);
	}

	void SrtStream::OnSrtPlaylistData(
		const std::weak_ptr<SrtPlaylist> &playlist,
		const std::shared_ptr<const ov::Data> &data)
	{
		auto srt_data = std::make_shared<const SrtData>(playlist, data);

		BroadcastPacket(std::make_any<std::shared_ptr<const SrtData>>(srt_data));

		MonitorInstance->IncreaseBytesOut(
			*GetSharedPtrAs<info::Stream>(),
			PublisherType::Srt,
			data->GetLength() * GetSessionCount());
	}
}  // namespace pub
