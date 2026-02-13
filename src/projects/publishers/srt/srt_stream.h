//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2024 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <base/common_types.h>
#include <base/publisher/stream.h>

#include "./srt_playlist.h"
#include "monitoring/monitoring.h"

namespace pub
{
	class SrtStream final : public Stream, public SrtPlaylistSink
	{
	public:
		static std::shared_ptr<SrtStream> Create(const std::shared_ptr<Application> application,
												 const info::Stream &info,
												 uint32_t worker_count);
		explicit SrtStream(const std::shared_ptr<Application> application,
						   const info::Stream &info,
						   uint32_t worker_count);
		~SrtStream() override final;

		std::shared_ptr<SrtPlaylist> GetSrtPlaylist(const ov::String &file_name);

		//--------------------------------------------------------------------
		// Implementation of info::Stream
		//--------------------------------------------------------------------
		std::shared_ptr<const pub::Stream::DefaultPlaylistInfo> GetDefaultPlaylistInfo() const override;
		//--------------------------------------------------------------------

		//--------------------------------------------------------------------
		// Overriding of Stream
		//--------------------------------------------------------------------
		void SendVideoFrame(const std::shared_ptr<MediaPacket> &media_packet) override;
		void SendAudioFrame(const std::shared_ptr<MediaPacket> &media_packet) override;
		void SendDataFrame(const std::shared_ptr<MediaPacket> &media_packet) override;
		//--------------------------------------------------------------------

		//--------------------------------------------------------------------
		// Implementation of SrtPlaylistSink
		//--------------------------------------------------------------------
		void OnSrtPlaylistData(
			const std::weak_ptr<SrtPlaylist> &playlist,
			const std::shared_ptr<const ov::Data> &data) override;
		//--------------------------------------------------------------------

	private:
		bool Start() override;
		bool Stop() override;

		bool IsSupportedTrack(const std::shared_ptr<MediaTrack> &track) const;

		std::shared_ptr<SrtPlaylist> GetSrtPlaylistInternal(const ov::String &file_name);

		std::map<int32_t, std::shared_ptr<MediaTrack>> GetSupportedTracks(const std::map<int32_t, std::shared_ptr<MediaTrack>> &track_map) const;
		std::vector<std::shared_ptr<MediaTrack>> GetSupportedTracks(const std::vector<std::shared_ptr<MediaTrack>> &tracks) const;
		std::vector<std::shared_ptr<MediaTrack>> GetSupportedTracks(const std::shared_ptr<MediaTrackGroup> &group) const;

		std::shared_ptr<const info::Playlist> PrepareDefaultPlaylist();
		// std::shared_ptr<const info::Playlist> CreatePlaylist(
		// 	const cfg::vhost::app::oprf::Playlist &playlist_config,
		// 	const TrackMapSet &track_map_set);
		void AddSupportedTrack(const std::shared_ptr<MediaTrack> &track, std::map<ov::String, std::shared_ptr<MediaTrack>> &to);

		void PrepareForTrack(
			const std::shared_ptr<MediaTrack> &track,
			std::map<uint32_t, std::shared_ptr<ov::Data>> &psi_data_map,
			std::map<uint32_t, std::shared_ptr<ov::Data>> &data_to_send_map);

		void EnqueuePacket(const std::shared_ptr<MediaPacket> &media_packet);
		void BroadcastIfReady(const std::vector<std::shared_ptr<mpegts::Packet>> &packets);

	private:
		uint32_t _worker_count = 0;

		std::shared_mutex _srt_playlist_map_mutex;
		// key: track id, value: list of playlist that uses the track
		std::map<int32_t, std::vector<std::shared_ptr<SrtPlaylist>>> _srt_playlist_map_by_track_id;
		// key: playlist file name, value: playlist
		std::map<ov::String, std::shared_ptr<SrtPlaylist>> _srt_playlist_map_by_file_name;
	};
}  // namespace pub
