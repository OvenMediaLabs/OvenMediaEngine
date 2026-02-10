//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2022 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <base/common_types.h>
#include <base/publisher/stream.h>
#include <modules/containers/mpegts/mpegts_packetizer.h>
#include <modules/containers/mpegts/mpegts_packager.h>
#include <modules/containers/webvtt/webvtt_packager.h>
#include <monitoring/monitoring.h>

#include <base/modules/data_format/cue_event/cue_event.h>

#include "hls_master_playlist.h"
#include "hls_media_playlist.h"

#include <modules/dump/dump.h>
#include <memory>
#include <map>
#include <shared_mutex>

// max initial media packet buffer size, for OOM protection
#define MAX_INITIAL_MEDIA_PACKET_BUFFER_SIZE 10000

class HlsSession;
class HlsStream final : public pub::Stream, public mpegts::PackagerSink
{
public:
	static std::shared_ptr<HlsStream> Create(const std::shared_ptr<pub::Application> application, 
												const info::Stream &info,
												uint32_t worker_count);

	explicit HlsStream(const std::shared_ptr<pub::Application> application, const info::Stream &info, uint32_t worker_count);
	~HlsStream() final;

	//--------------------------------------------------------------------
	// Implementation of info::Stream
	//--------------------------------------------------------------------
	std::shared_ptr<const pub::Stream::DefaultPlaylistInfo> GetDefaultPlaylistInfo() const override;
	//--------------------------------------------------------------------

	void SendVideoFrame(const std::shared_ptr<MediaPacket> &media_packet) override;
	void SendAudioFrame(const std::shared_ptr<MediaPacket> &media_packet) override;
	void SendDataFrame(const std::shared_ptr<MediaPacket> &media_packet) override;
	void OnEvent(const std::shared_ptr<MediaEvent> &event) override;

	enum class RequestResult : uint8_t
	{
		Success, // Success
		NotFound, // The request is not found
		UnknownError,
	};

	// Implement mpegts::PackagerSink
	void OnSegmentCreated(const ov::String &packager_id, const std::shared_ptr<base::modules::Segment> &segment) override;
	void OnSegmentDeleted(const ov::String &packager_id, const std::shared_ptr<base::modules::Segment> &segment) override;

	// Interface for HLS Session
	std::tuple<RequestResult, std::shared_ptr<const ov::Data>> GetMasterPlaylistData(const ov::String &playlist_name, bool rewind);
	std::tuple<RequestResult, std::shared_ptr<const ov::Data>> GetMediaPlaylistData(const ov::String &variant_name, bool rewind);
	std::tuple<RequestResult, std::shared_ptr<const ov::Data>> GetSegmentData(const ov::String &variant_name, uint32_t number);

	ov::String GetStreamId() const;

	/// Origin Mode Session Management
	std::shared_ptr<HlsSession> GetSessionFromPool();

private:
	bool Start() override;
	bool Stop() override;
	bool IsSupportedCodec(cmn::MediaCodecId codec_id) const; 

	bool CreateDefaultPlaylist();
	bool CreatePackagers();

	ov::String GetVariantName(const ov::String &video_variant_name, int video_index, const ov::String &audio_variant_name, int audio_index) const;
	ov::String GetMediaPlaylistName(const ov::String &variant_name) const;
	ov::String GetSegmentName(const ov::String &variant_name, uint32_t number) const;

	std::shared_ptr<mpegts::Packetizer> GetTSPacketizer(const ov::String &variant_name);
	std::shared_ptr<mpegts::Packager> GetTSPackager(const ov::String &variant_name);
	std::shared_ptr<HlsMediaPlaylist> GetMediaPlaylist(const ov::String &variant_name);
	std::shared_ptr<HlsMasterPlaylist> GetMasterPlaylist(const ov::String &playlist_name);

	bool AppendMediaPacket(const std::shared_ptr<MediaPacket> &media_packet);
	void BufferMediaPacketUntilReadyToPlay(const std::shared_ptr<MediaPacket> &media_packet);
	bool SendBufferedPackets();

	bool CheckIfAllPlaylistReady();

	ov::Queue<std::shared_ptr<MediaPacket>> _initial_media_packet_buffer;

	//////////////////////////
	// Events
	//////////////////////////

	// <result, error message>
	std::tuple<bool, ov::String> ConcludeLive();
	bool IsConcluded() const;

	//////////////////////////
	// Dumps
	//////////////////////////

	void InitializeAllDumps();
	void DumpMasterPlaylistOfAllItems();
	bool DumpMasterPlaylist(const std::shared_ptr<mdl::Dump>& item);
	void DumpSegmentOfAllItems(const ov::String &packager_id, const uint32_t &segment_number);
	bool DumpSegment(const std::shared_ptr<mdl::Dump>& dump, const ov::String& packager_id, const uint32_t& segment_number);
	bool DumpData(const std::shared_ptr<mdl::Dump>& dump, const ov::String& file_name, const std::shared_ptr<const ov::Data>& data, bool append = false);
	void StopDumps();
	void StopDump(const std::shared_ptr<mdl::Dump>& dumper);

	uint32_t _worker_count = 0;

	cfg::vhost::app::pub::HlsPublisher _ts_config;
	// default querystring value
	bool _default_option_rewind = true;

	// packetizer id : Packetizer
	std::map<ov::String, std::shared_ptr<mpegts::Packetizer>> _ts_packetizers;
	// All packetizers for each track
	std::map<uint32_t, std::vector<std::shared_ptr<mpegts::Packetizer>>> _track_packetizers;
	std::shared_mutex _ts_packetizers_guard;

	std::map<ov::String, std::shared_ptr<mpegts::Packager>> _ts_packagers;
	std::shared_mutex _ts_packagers_guard;

	// playlist name : Master playlist
	std::map<ov::String, std::shared_ptr<HlsMasterPlaylist>> _master_playlists;
	std::shared_mutex _master_playlists_guard;

	// variant name : Media playlist
	std::map<ov::String, std::shared_ptr<HlsMediaPlaylist>> _media_playlists;
	mutable std::shared_mutex _media_playlists_guard;

	// ConcludeLive
	// Append #EXT-X-ENDLIST all chunklists, and no more update segment and chunklist
	bool _concluded = false;
	mutable std::shared_mutex _concluded_lock;

	// Dumps map
	std::map<ov::String, std::shared_ptr<mdl::Dump>> _dumps;
	std::shared_mutex _dumps_lock;

	bool _ready_to_play = false; // true if the stream is ready to play, all playlists are ready and have enough segments

	bool CreateOriginSessionPool();
	bool _origin_mode = true;

	// Subtitles, vtt
	bool AddVttPackager(const std::shared_ptr<const MediaTrack> &track);
	std::shared_ptr<webvtt::Packager> GetVttPackager(const int32_t &track_id) const;
	ov::String MakeVttVariantName(const int32_t &track_id) const;

	std::map<int32_t, std::shared_ptr<webvtt::Packager>> GetVttPackagers() const;

	bool _vtt_enabled = false;
	ov::String _vtt_reference_packager_id = ""; 
	std::map<int32_t, std::shared_ptr<webvtt::Packager>> _vtt_packagers;
	mutable std::shared_mutex _vtt_packagers_lock;

	// mpegts packager, vtt packager
	std::map<ov::String, std::shared_ptr<base::modules::SegmentStorage>> _storage_map;
	mutable std::shared_mutex _storage_map_guard;

	std::shared_ptr<base::modules::SegmentStorage> GetStorage(const ov::String &variant_name) const;
};
