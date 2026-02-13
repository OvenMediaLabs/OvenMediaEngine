//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2025 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <base/info/media_track_group.h>
#include <base/info/playlist.h>
#include <base/info/stream.h>
#include <base/mediarouter/media_buffer.h>
#include <base/ovlibrary/ovlibrary.h>
#include <modules/containers/mpegts/mpegts_packetizer.h>

namespace pub
{
	class SrtPlaylist;

	class SrtPlaylistSink
	{
	public:
		virtual ~SrtPlaylistSink() = default;

		virtual void OnSrtPlaylistData(
			const std::weak_ptr<SrtPlaylist> &playlist,
			const std::shared_ptr<const ov::Data> &data) = 0;
	};

	struct SrtData
	{
		SrtData(
			const std::weak_ptr<SrtPlaylist> &playlist,
			const std::shared_ptr<const ov::Data> &data)
			: playlist(playlist),
			  data(data)
		{
		}

		// The playlist that this data belongs to
		std::weak_ptr<SrtPlaylist> playlist;

		// The data to send
		std::shared_ptr<const ov::Data> data;
	};

	// SrtPlaylist IS NOT thread safe, so it should be used with a lock if needed
	class SrtPlaylist : public mpegts::PacketizerSink, public ov::EnableSharedFromThis<SrtPlaylistSink>
	{
	public:
		SrtPlaylist(
			const std::shared_ptr<const info::Stream> &stream_info,
			const std::shared_ptr<const info::Playlist> &playlist_info,
			const std::shared_ptr<SrtPlaylistSink> &sink);

		void AddTrack(const std::shared_ptr<MediaTrack> &track);
		void AddTracks(const std::vector<std::shared_ptr<MediaTrack>> &tracks);

		bool Start();
		bool Stop();

		void EnqueuePacket(const std::shared_ptr<MediaPacket> &media_packet);

		//--------------------------------------------------------------------
		// Implementation of mpegts::PacketizerSink
		//--------------------------------------------------------------------
		// Do not need to lock _packetizer_mutex inside OnPsi() because it will be called only once when the packetizer starts
		void OnPsi(const std::vector<std::shared_ptr<const MediaTrack>> &tracks, const std::vector<std::shared_ptr<mpegts::Packet>> &psi_packets) override;
		// Do not need to lock _packetizer_mutex inside OnFrame() because it's called after acquiring the lock in EnqueuePacket()
		// (It's called in the thread that calls EnqueuePacket())
		void OnFrame(const std::shared_ptr<const MediaPacket> &media_packet, const std::vector<std::shared_ptr<mpegts::Packet>> &pes_packets) override;
		//--------------------------------------------------------------------

		const std::shared_ptr<const ov::Data> &GetPsiData() const
		{
			return _psi_data;
		}

	private:
		struct TrackInfo
		{
			std::shared_ptr<MediaTrack> track;

			bool first_key_frame_received = false;

			TrackInfo(const std::shared_ptr<MediaTrack> &track)
				: track(track)
			{
			}
		};

	private:
		void SendData(const std::vector<std::shared_ptr<mpegts::Packet>> &packets);

	private:
		std::shared_ptr<const info::Stream> _stream_info;
		std::shared_ptr<const info::Playlist> _playlist_info;

		std::unordered_map<int32_t, TrackInfo> _track_info_map;

		std::shared_ptr<mpegts::Packetizer> _packetizer;

		std::shared_ptr<SrtPlaylistSink> _sink;

		std::shared_ptr<const ov::Data> _psi_data;
		std::shared_ptr<ov::Data> _data_to_send = std::make_shared<ov::Data>();
	};
}  // namespace pub
