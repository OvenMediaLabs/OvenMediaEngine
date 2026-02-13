//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2025 AirenSoft. All rights reserved.
//
//==============================================================================
#include "srt_playlist.h"

#include <base/info/playlist.h>
#include <base/info/stream.h>
#include <base/ovsocket/socket.h>

#include "srt_private.h"

#define OV_LOG_PREFIX_FORMAT "[%s/%s(%u)] "
#define OV_LOG_PREFIX_VALUE _stream_info->GetApplicationName(), _stream_info->GetName().CStr(), _stream_info->GetId()

namespace pub
{
	SrtPlaylist::SrtPlaylist(
		const std::shared_ptr<const info::Stream> &stream_info,
		const std::shared_ptr<const info::Playlist> &playlist_info,
		const std::shared_ptr<SrtPlaylistSink> &sink)
		: _stream_info(stream_info),
		  _playlist_info(playlist_info),
		  _sink(sink)
	{
		_packetizer = std::make_shared<mpegts::Packetizer>();
	}

	void SrtPlaylist::AddTrack(const std::shared_ptr<MediaTrack> &track)
	{
		// Duplicated tracks will be ignored by the packetizer
		_packetizer->AddTrack(track);

		_track_info_map.emplace(track->GetId(), track);
	}

	void SrtPlaylist::AddTracks(const std::vector<std::shared_ptr<MediaTrack>> &tracks)
	{
		std::for_each(tracks.begin(), tracks.end(), std::bind(&SrtPlaylist::AddTrack, this, std::placeholders::_1));
	}

	bool SrtPlaylist::Start()
	{
		return _packetizer->AddSink(GetSharedPtrAs<mpegts::PacketizerSink>()) && _packetizer->Start();
	}

	bool SrtPlaylist::Stop()
	{
		return _packetizer->Stop() && _packetizer->RemoveSink(GetSharedPtrAs<mpegts::PacketizerSink>());
	}

	void SrtPlaylist::EnqueuePacket(const std::shared_ptr<MediaPacket> &media_packet)
	{
		auto track_info_iterator = _track_info_map.find(media_packet->GetTrackId());

		if (track_info_iterator == _track_info_map.end())
		{
			logte("The track is not found in the playlist map");
			OV_ASSERT2(false);
			return;
		}

		auto &track_info = track_info_iterator->second;

		if (track_info.first_key_frame_received == false)
		{
			track_info.first_key_frame_received = media_packet->IsKeyFrame();
		}

		if (track_info.first_key_frame_received)
		{
			_packetizer->AppendFrame(media_packet);
		}
	}

	void SrtPlaylist::SendData(const std::vector<std::shared_ptr<mpegts::Packet>> &packets)
	{
		if (_sink == nullptr)
		{
			return;
		}

		auto self = GetSharedPtrAs<SrtPlaylist>();

		for (auto &packet : packets)
		{
			auto size		 = _data_to_send->GetLength();
			const auto &data = packet->GetData();

			// Broadcast if the data size exceeds the SRT's payload length
			if ((size + data->GetLength()) > SRT_LIVE_DEF_PLSIZE)
			{
				_sink->OnSrtPlaylistData(self, _data_to_send);
				_data_to_send = data->Clone();
			}
			else
			{
				_data_to_send->Append(packet->GetData());
			}
		}
	}

	void SrtPlaylist::OnPsi(const std::vector<std::shared_ptr<const MediaTrack>> &tracks, const std::vector<std::shared_ptr<mpegts::Packet>> &psi_packets)
	{
		std::shared_ptr<ov::Data> psi_data = std::make_shared<ov::Data>();

		// Concatenate PSI packets
		for (const auto &packet : psi_packets)
		{
			psi_data->Append(packet->GetData());
		}

		logap("OnPsi - %zu packets (total %zu bytes)", psi_packets.size(), psi_data->GetLength());

		_psi_data = std::move(psi_data);

		SendData(psi_packets);
	}

	void SrtPlaylist::OnFrame(const std::shared_ptr<const MediaPacket> &media_packet, const std::vector<std::shared_ptr<mpegts::Packet>> &pes_packets)
	{
#if DEBUG
		// Since adding up the total packet size is costly, it is calculated only in debug mode
		size_t total_packet_size = 0;

		for (const auto &packet : pes_packets)
		{
			total_packet_size += packet->GetDataLength();
		}

		logap("OnFrame - %zu packets (total %zu bytes)", pes_packets.size(), total_packet_size);
#endif	// DEBUG

		SendData(pes_packets);
	}
}  // namespace pub
