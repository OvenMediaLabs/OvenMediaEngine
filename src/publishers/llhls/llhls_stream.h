//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2022 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <optional>

#include <base/common_types.h>
#include <base/publisher/stream.h>
#include <base/info/dump.h>
#include <modules/dump/dump.h>

#include "monitoring/monitoring.h"

#include "modules/containers/bmff/fmp4_packager/fmp4_packager.h"
#include "modules/containers/webvtt/webvtt_packager.h"
#include "llhls_master_playlist.h"
#include "llhls_chunklist.h"

// max initial media packet buffer size, for OOM protection
#define MAX_INITIAL_MEDIA_PACKET_BUFFER_SIZE		10000

class LLHlsSession;
class LLHlsStream final : public pub::Stream, public bmff::FMp4StorageObserver
{
public:
	static std::shared_ptr<LLHlsStream> Create(const std::shared_ptr<pub::Application> application, 
												const info::Stream &info,
												bool origin_mode,
												uint32_t worker_count);

	explicit LLHlsStream(const std::shared_ptr<pub::Application> application, const info::Stream &info, bool origin_mode, uint32_t worker_count);
	~LLHlsStream() final;

	ov::String GetStreamId() const;

	//--------------------------------------------------------------------
	// Implementation of info::Stream
	//--------------------------------------------------------------------
	std::shared_ptr<const DefaultPlaylistInfo> GetDefaultPlaylistInfo() const override;
	//--------------------------------------------------------------------

	void SendVideoFrame(const std::shared_ptr<MediaPacket> &media_packet) override;
	void SendAudioFrame(const std::shared_ptr<MediaPacket> &media_packet) override;
	void SendDataFrame(const std::shared_ptr<MediaPacket> &media_packet) override;
	void OnEvent(const std::shared_ptr<MediaEvent> &event) override;
	void OnTrackChanged(int32_t track_id, const std::shared_ptr<const MediaTrack> &old_track, const std::shared_ptr<const MediaTrack> &new_track) override;

	enum class RequestResult : uint8_t
	{
		Success, // Success
		Accepted, // The request is accepted but not yet processed, it will be processed later
		NotFound, // The request is not found
		UnknownError,
	};

	struct PlaylistUpdatedEvent
	{
		PlaylistUpdatedEvent(const int32_t &id, const int64_t &new_msn, const int64_t &new_part)
		{
			track_id = id;
			msn = new_msn;
			part = new_part;
		}

		int32_t track_id;
		int64_t msn;
		int64_t part;
	};
	
	const ov::String &GetStreamKey() const;

	std::tuple<RequestResult, std::shared_ptr<const ov::Data>> GetMasterPlaylist(const ov::String &file_name, const ov::String &chunk_query_string, bool gzip, bool legacy, bool rewind, bool include_path=true);
	std::tuple<RequestResult, std::shared_ptr<const ov::Data>> GetChunklist(const ov::String &chunk_query_string, const int32_t &track_id, int64_t msn, int64_t psn, bool skip, bool gzip, bool legacy, bool rewind) const;
	std::tuple<RequestResult, std::shared_ptr<ov::Data>> GetInitializationSegment(const int32_t &track_id) const;
	std::tuple<RequestResult, std::shared_ptr<ov::Data>> GetInitializationSegment(const int32_t &track_id, uint32_t track_version) const;
	std::tuple<RequestResult, std::shared_ptr<ov::Data>> GetSegment(const int32_t &track_id, const int64_t &segment_number) const;
	std::tuple<RequestResult, std::shared_ptr<ov::Data>> GetPartial(const int32_t &track_id, const int64_t &segment_number, const int64_t &chunk_number) const;

	//////////////////////////
	// For Dump API
	//////////////////////////

	// <result, error message>
	std::tuple<bool, ov::String> StartDump(const std::shared_ptr<info::Dump> &dump_info);
	std::tuple<bool, ov::String> StopDump(const std::shared_ptr<info::Dump> &dump_info);
	// Get dump info
	std::shared_ptr<const mdl::Dump> GetDumpInfo(const ov::String &dump_id);
	// Get dumps
	std::vector<std::shared_ptr<const mdl::Dump>> GetDumpInfoList();

	//////////////////////////
	// Check marker can be inserted
	//////////////////////////
	std::tuple<bool, ov::String> CanInsertMarker(cmn::BitstreamFormat bitstream_format, int64_t timestamp_ms, const std::shared_ptr<ov::Data> &data) const;

	/// Origin Mode Session Management
	std::shared_ptr<LLHlsSession> GetSessionFromPool();

private:
	bool Start() override;
	bool Stop() override;

	// Everything the DRM info file states for the matched stream
	struct DrmInfo
	{
		// What <DRMProvider> states, settled on one spelling. Left out, the file means
		// "manual", the provider that keeps its keys in the file itself.
		ov::String provider;
		// How the media is encrypted, and which DRM systems the stream offers
		bmff::CencProtectScheme scheme = bmff::CencProtectScheme::None;
		bmff::DRMSystem drm_system	   = bmff::DRMSystem::None;
		// What <KeyRotationPeriod> states, in seconds of stream time. Left out, the keys
		// never change; 0 rotates only when asked to; above 0 rotates on that period.
		std::optional<uint64_t> rotation_period_sec;

		// Keys the manual provider rotates through, in order. A single flat key yields a
		// one-entry list, and the list stays empty when the keys are served from elsewhere
		// instead.
		std::vector<bmff::CencProperty> key_list;

		// Where to ask for the key of each period, for a provider that serves them
		ov::String kms_url;
		ov::String kms_token;
		ov::String content_id;
	};

	// Parse the DRM info file into what it states for the matched stream. Parsed as a
	// whole, so a file that fails midway commits nothing.
	bool GetDrmInfo(const ov::String &file_path, DrmInfo &drm_info);

	// Rotate the DRM key, from the next segment of each track. Triggered by the auto
	// rotation period and by a RotateDrmKey event. The keys of the file are walked as a
	// cycle, and a provider that serves keys hands over the one already fetched for the
	// next period, if it is ready.
	void RotateDrmKey();

	// Trigger an auto key rotation once the media time crosses a period boundary.
	// Called as segments are produced; a no-op when auto rotation is off.
	void CheckAutoKeyRotation(int64_t media_time_ms);

	bool IsSupportedMediaCodec(cmn::MediaCodecId codec_id) const; 

	void NotifyPlaylistUpdated(const int32_t &track_id, const int64_t &msn, const int64_t &part);

	// bmff::FMp4StorageObserver implementation
	void OnFMp4StorageInitialized(const int32_t &track_id) override;
	void OnMediaSegmentCreated(const int32_t &track_id, const uint32_t &segment_number) override;
	void OnMediaChunkUpdated(const int32_t &track_id, const uint32_t &segment_number, const uint32_t &chunk_number, bool last_chunk) override;
	void OnMediaSegmentDeleted(const int32_t &track_id, const uint32_t &segment_number) override;
	void OnMediaSegmentCompleted(const int32_t &track_id, const uint32_t &segment_number) override;

	// Create and Get fMP4 packager and storage with track info, storage and packager_config
	bool AddPackager(const std::shared_ptr<const MediaTrack> &media_track, const std::shared_ptr<const MediaTrack> &data_track);

	// Get fMP4 packager with the track id
	std::shared_ptr<bmff::FMP4Packager> GetPackager(const int32_t &track_id) const;
	// Get storage with the track id
	std::shared_ptr<base::modules::SegmentStorage> GetStorage(const int32_t &track_id) const;
	// Get storage of a fMP4 media track (VTT tracks use a different storage type)
	std::shared_ptr<bmff::FMP4Storage> GetFmp4Storage(const int32_t &track_id) const;
	// Get Playlist with the track id
	std::shared_ptr<LLHlsChunklist> GetChunklistWriter(const int32_t &track_id) const;

	std::shared_ptr<LLHlsMasterPlaylist> CreateMasterPlaylist(const std::shared_ptr<const info::Playlist> &playlist, bool include_unlisted_codecs = false) const;

	// Uncached master playlist for dumped output; its CODECS attribute covers
	// unlisted versions too because the dump keeps segments of every version
	std::tuple<RequestResult, std::shared_ptr<const ov::Data>> GetMasterPlaylistForDump(const ov::String &file_name) const;

	// Codecs the CODECS attribute advertises for a track, answered by the track's
	// chunklist from its own listing (or registration history for dumped output)
	ov::String GetCodecsParameterUnion(const std::shared_ptr<const MediaTrack> &track, bool include_unlisted = false) const;

	ov::String GetChunklistName(const int32_t &track_id) const;
	ov::String GetInitializationSegmentName(const int32_t &track_id) const;
	ov::String GetInitializationSegmentName(const int32_t &track_id, uint32_t track_version) const;
	// Map uri a segment of the given version references; the initial version keeps
	// the version-less name, tracks without a fMP4 packager have none
	ov::String GetMapUriForTrackVersion(const int32_t &track_id, uint32_t track_version) const;
	ov::String GetSegmentName(const int32_t &track_id, const int64_t &segment_number) const;
	ov::String GetPartialSegmentName(const int32_t &track_id, const int64_t &segment_number, const int64_t &partial_number) const;
	ov::String GetNextPartialSegmentName(const int32_t &track_id, const int64_t &segment_number, const int64_t &partial_number, bool last_chunk) const;

	bool AppendMediaPacket(const std::shared_ptr<MediaPacket> &media_packet);

	bool IsReadyToPlay() const;
	bool CheckPlaylistReady();

	void DumpMasterPlaylistsOfAllItems();
	bool DumpMasterPlaylist(const std::shared_ptr<mdl::Dump> &item);
	void DumpInitSegmentOfAllItems(const int32_t &track_id);
	bool DumpInitSegment(const std::shared_ptr<mdl::Dump> &item, const int32_t &track_id);
	void DumpSegmentOfAllItems(const int32_t &track_id, const uint32_t &segment_number);
	bool DumpSegment(const std::shared_ptr<mdl::Dump> &item, const int32_t &track_id, const int64_t &segment_number);

	bool DumpData(const std::shared_ptr<mdl::Dump> &item, const ov::String &file_name, const std::shared_ptr<const ov::Data> &data);

	int64_t GetMinimumLastSegmentNumber() const;
	bool StopToSaveOldSegmentsInfo();

	double ComputeOptimalPartDuration(const std::shared_ptr<const MediaTrack> &track) const;

	// Events
	// <result, error message>
	std::tuple<bool, ov::String> ConcludeLive();
	bool IsConcluded() const;

	bool InsertMarkerToAllPackagers(uint32_t data_track_id, cmn::BitstreamFormat bitstream_format, int64_t timestamp_ms, const std::shared_ptr<ov::Data> &data);

	// Config
	bmff::FMP4Packager::Config _packager_config;
	bmff::FMP4Storage::Config _storage_config;

	// Track ID : Storage
	std::map<int32_t, std::shared_ptr<base::modules::SegmentStorage>> _storage_map;
	mutable std::shared_mutex _storage_map_lock;
	std::map<int32_t, std::shared_ptr<bmff::FMP4Packager>> _packager_map;
	mutable std::shared_mutex _packager_map_lock;
	std::map<int32_t, std::shared_ptr<LLHlsChunklist>> _chunklist_map;
	mutable std::shared_mutex _chunklist_map_lock;

	// Track ID : track version the stream started with (uses the version-less
	// initialization segment name, later versions carry a version suffix)
	std::map<int32_t, uint32_t> _initial_track_versions;
	mutable std::shared_mutex _initial_track_versions_lock;


	uint64_t _max_chunk_duration_ms = 0;
	uint64_t _min_chunk_duration_ms = std::numeric_limits<uint64_t>::max();

	double _configured_part_hold_back = 0;
	bool _preload_hint_enabled = true;

	std::map<ov::String, std::shared_ptr<LLHlsMasterPlaylist>> _master_playlists;
	std::mutex _master_playlists_lock;

	bool _playlist_ready = false;
	mutable std::shared_mutex _playlist_ready_lock;

	// Reserve
	void BufferMediaPacketUntilReadyToPlay(const std::shared_ptr<MediaPacket> &media_packet);
	bool SendBufferedPackets();
	ov::Queue<std::shared_ptr<MediaPacket>> _initial_media_packet_buffer;

	bool _origin_mode = false;
	ov::String _stream_key;

	uint32_t _worker_count = 0;

	std::map<ov::String, std::shared_ptr<mdl::Dump>> _dumps;
	std::shared_mutex _dumps_lock;

	// DRM
	[[maybe_unused]] bool _indentity_enabled = false; // for custom license server and player purposes
	[[maybe_unused]] bool _widevine_enabled = false;
	[[maybe_unused]] bool _playready_enabled = false;
	[[maybe_unused]] bool _fairplay_enabled = false;

	bmff::CencProperty _cenc_property;
	ov::String _key_uri; // string, only for FairPlay

	// Path of the DRM info file, read once as the stream starts
	ov::String _drm_info_path;
	// Media time of the last rotation boundary, to space out auto rotations. Advanced
	// whether or not a key was there to rotate to, so a period without one is skipped
	// rather than retried at every segment. Guarded by _cenc_lock.
	int64_t _last_key_rotation_media_time_ms = -1;
	// A rotation the configuration cannot serve is reported once, not every period.
	// Guarded by _cenc_lock.
	bool _rotation_unavailable_warned = false;

	// Guarded by _cenc_lock.
	DrmInfo _drm_info;
	// Key period the current key belongs to, advanced by one on every rotation.
	// Guarded by _cenc_lock.
	uint64_t _key_period_index = 0;
	// Key of the next period, held until a rotation applies it. Guarded by _cenc_lock.
	std::optional<bmff::CencProperty> _prefetched_key;
	// A key request is on its way. Guarded by _cenc_lock.
	bool _key_request_in_flight = false;
	// Media time before which no further key request is made, so a source that is failing
	// is not asked once per segment. Guarded by _cenc_lock.
	int64_t _next_key_request_media_time_ms = 0;

	mutable std::shared_mutex _cenc_lock;

	// Hands the new key to every track and refreshes the master playlists
	void ApplyRotatedKey(const bmff::CencProperty &cenc_property);

	// Returns the key a period should use, waiting for it, so it runs while the stream
	// starts or on a task pool worker, never on the media thread. The keys of the file are
	// read from it, and a provider that serves its keys is asked for them.
	bool RequestKeyOfPeriod(uint64_t key_period_index, bmff::CencProperty &cenc_property);
	// Hands the request for the key of the next period to a task pool worker unless one is
	// already waiting or on its way. Returns at once, so it can run on the media thread.
	void PrefetchNextKeyIfNeeded(int64_t media_time_ms);
	// Stores a key that PrefetchNextKeyIfNeeded asked for. Runs on the task pool worker that
	// made the request.
	void OnKeyPrefetched(uint64_t key_period_index, bool succeeded, const bmff::CencProperty &cenc_property, int64_t requested_media_time_ms);

	// Highest content version already advertised to each track's chunklist, so a new
	// version's EXT-X-KEY is registered once when its first segment appears.
	// Media-thread only (the storage observer callbacks).
	std::map<int32_t, uint32_t> _last_registered_cenc_version;

	// PROGRAM-DATE-TIME
	bool _first_chunk = true;
	int64_t _wallclock_offset_ms = 0;

	// ConcludeLive
	// Append #EXT-X-ENDLIST all chunklists, and no more update segment and chunklist
	bool _concluded = false;
	mutable std::shared_mutex _concluded_lock;

	// Subtitles, vtt
	bool IsVttEnabled() const;
	bool AddVttPackager(const std::shared_ptr<const MediaTrack> &track);
	std::shared_ptr<webvtt::Packager> GetVttPackager(const int32_t &track_id) const;
	std::map<int32_t, std::shared_ptr<webvtt::Packager>> GetVttPackagers() const;

	bool _vtt_enabled = false;
	int32_t _vtt_reference_track_id = -1; // track id of the reference track for VTT

	std::map<int32_t, std::shared_ptr<webvtt::Packager>> _vtt_packagers;
	mutable std::shared_mutex _vtt_packagers_lock;

	bool CreateOriginSessionPool();
};