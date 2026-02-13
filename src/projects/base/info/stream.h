#pragma once

#include <config/config_manager.h>

#include "base/common_types.h"
#include "base/info/media_track_group.h"
#include "base/info/playlist.h"
#include "vhost_app_name.h"

namespace info
{
	typedef uint32_t stream_id_t;
	constexpr stream_id_t InvalidStreamId = std::numeric_limits<stream_id_t>::max();
	constexpr stream_id_t MinStreamId = std::numeric_limits<stream_id_t>::min();
	constexpr stream_id_t MaxStreamId = (InvalidStreamId - static_cast<stream_id_t>(1));

	class Application;

	//TODO: It should be changed class name to Stream
	class Stream
	{
	public:
		Stream(StreamSourceType source);
		Stream(const info::Application &app_info, StreamSourceType source);
		Stream(const info::Application &app_info, info::stream_id_t stream_id, StreamSourceType source);
		Stream(const Stream &stream);
		virtual ~Stream();

		bool operator==(const Stream &stream_info) const;

		NamePath GetNamePath() const;

		void SetId(info::stream_id_t id);
		info::stream_id_t GetId() const;

		// Get Stream Resource ID in ovenmediaengine (vhost#app/stream)
		ov::String GetUri() const;

		void SetMsid(uint32_t);
		uint32_t GetMsid();

		ov::String GetUUID() const;
		ov::String GetName() const;
		void SetName(ov::String name);

		ov::String GetMediaSource() const;
		void SetMediaSource(ov::String url);

		void SetOutputProfileName(ov::String name);
		ov::String GetOutputProfileName() const;

		bool IsInputStream() const;
		bool IsOutputStream() const;

		void LinkInputStream(const std::shared_ptr<Stream> &stream);
		const std::shared_ptr<Stream> GetLinkedInputStream() const;

		// Only used in OVT provider
		void SetOriginStreamUUID(const ov::String &uuid);
		ov::String GetOriginStreamUUID() const;

		const std::chrono::system_clock::time_point &GetInputStreamCreatedTime() const;
		const std::chrono::system_clock::time_point &GetCreatedTime() const;

		void SetPublishedTime(const std::chrono::system_clock::time_point &time);
		const std::chrono::system_clock::time_point &GetInputStreamPublishedTime() const;
		const std::chrono::system_clock::time_point &GetPublishedTime() const;

		uint32_t GetUptimeSec();
		StreamSourceType GetSourceType() const;
		ProviderType GetProviderType() const;
		
		StreamRepresentationType GetRepresentationType() const;
		void SetRepresentationType(const StreamRepresentationType &type);

		uint32_t IssueUniqueTrackId();
		bool AddTrack(const std::shared_ptr<MediaTrack> &track);
		bool UpdateTrack(const std::shared_ptr<MediaTrack> &track);
		bool RemoveTrack(uint32_t id);
		
		const std::shared_ptr<MediaTrack> GetTrack(int32_t id) const;
		const std::shared_ptr<MediaTrack> GetTrackByLabel(const ov::String &public_label) const;
		const std::map<int32_t, std::shared_ptr<MediaTrack>> &GetTracks() const;

		const std::shared_ptr<MediaTrackGroup> GetMediaTrackGroup(const ov::String &group_name) const;
		// Get Track Groups
		const std::map<ov::String, std::shared_ptr<MediaTrackGroup>> &GetMediaTrackGroups() const;

		// Get number of tracks
		// Get track nth
		// @param order : 0 ~ (track count - 1)
		uint32_t GetMediaTrackCount(const cmn::MediaType &type) const;
		const std::shared_ptr<MediaTrack> GetMediaTrackByOrder(const cmn::MediaType &type, uint32_t order) const;
		
		const std::shared_ptr<MediaTrack> GetFirstTrackByType(const cmn::MediaType &type) const;
		const std::shared_ptr<MediaTrack> GetFirstTrackByVariant(const ov::String &name) const;
		const std::shared_ptr<MediaTrack> GetTrackByVariant(const ov::String &variant_name, uint32_t order) const;

		bool AddPlaylist(const std::shared_ptr<const Playlist> &playlist);
		std::shared_ptr<const Playlist> GetPlaylist(const ov::String &file_name) const;
		const std::map<ov::String, std::shared_ptr<const Playlist>> &GetPlaylists() const;

		ov::String GetInfoString();
		void ShowInfo();

		void SetApplicationInfo(const std::shared_ptr<Application> &app_info);
		const Application &GetApplicationInfo() const
		{
			return *_app_info;
		}

		const char *GetApplicationName();
		const char *GetApplicationName() const;

		bool HasVideoTrack() const
		{
			return _video_tracks.size() > 0;
		}

		bool HasAudioTrack() const
		{
			return _audio_tracks.size() > 0;
		}

		bool IsFromOriginMapStore() const
		{
			return _from_origin_map_store;
		}

		bool IsOnAir() const
		{
			return _on_air;
		}

		void SetOnAir(bool on_air)
		{
			_on_air = on_air;

			if (_on_air)
			{
				_published_time = std::chrono::system_clock::now();
			}
		}

		void SetTimestampMode(TimestampMode mode)
		{
			_timestamp_mode = mode;
		}

		TimestampMode GetTimestampMode() const
		{
			return _timestamp_mode;
		}

	protected:
		// Update name path from given `vhost_app_name` and `_name`
		void UpdateNamePath(const info::VHostAppName &vhost_app_name);
		// Update name path using `_app_info` and `_name`
		void UpdateNamePath();

	protected:
		info::stream_id_t _id = 0;
		uint32_t _msid = 0;
		ov::String _name;
		ov::String _source_url;
		ov::String _output_profile_name;
		
		// Key : MediaTrack ID
		std::map<int32_t, std::shared_ptr<MediaTrack>> _tracks; // For fast access by ID
		std::vector<std::shared_ptr<MediaTrack>> _audio_tracks; // For fast access by order
		std::vector<std::shared_ptr<MediaTrack>> _video_tracks; // For fast access by order

		// Group Name (variant name) : MediaTrackGroup
		std::map<ov::String, std::shared_ptr<MediaTrackGroup>> _track_group_map; // Track group

		// Subtitle label : track id
		std::map<ov::String, int32_t> _public_label_map; // Subtitle label map

		// File name : Playlist
		std::map<ov::String, std::shared_ptr<const Playlist>> _playlists;

		bool _from_origin_map_store = false;

	private:
		mutable std::mutex _name_path_mutex;
		NamePath _name_path = NamePath::UnknownNamePath();

		std::chrono::system_clock::time_point _created_time;
		std::chrono::system_clock::time_point _published_time;

		// Where does the stream come from?
		StreamSourceType _source_type;

		// Defines the purpose of this stream. Stream for relay? Stream for source?
		// Source Type : [Provider -> Transcoder -> Publisher]
		// 		- Affected by Output Profile.
		// Relay Type : [Provider -> Publisher]
		// 		- It is sent directly to the Publisher without affecting the Output Profile.
		StreamRepresentationType _representation_type = StreamRepresentationType::Source;

		std::shared_ptr<Application> _app_info = nullptr;

		// If the Source Type of this stream is LiveTranscoder,
		// the original stream coming from the Provider can be recognized with _origin_stream.
		std::shared_ptr<Stream> _origin_stream = nullptr;

		// If the source if this stream is a remote stream of the origin server, store the uuid of origin stream
		ov::String _origin_stream_uuid;

		bool _on_air = false;

		TimestampMode _timestamp_mode = TimestampMode::Auto;
	};
}  // namespace info