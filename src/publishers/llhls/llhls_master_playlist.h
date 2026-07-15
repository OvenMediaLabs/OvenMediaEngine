//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2022 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <base/ovlibrary/ovlibrary.h>
#include <base/info/media_track_group.h>
#include <base/mediarouter/media_buffer.h>
#include <modules/containers/bmff/cenc.h>

class LLHlsMasterPlaylist
{
public:
	struct MediaInfo
	{
		ov::String GetTypeStr() const
		{
			switch (_type)
			{
				case cmn::MediaType::Video:
					return "VIDEO";
				case cmn::MediaType::Audio:
					return "AUDIO";
				case cmn::MediaType::Subtitle:
					return "SUBTITLES";
				default:
					return "UNKNOWN";
			}
		}

		ov::String _group_id; // Required
		cmn::MediaType _type = cmn::MediaType::Unknown; // Required
		ov::String _name; // Required
		ov::String _language; // Optional
		ov::String _characteristics; // Optional
		bool _auto_select = true; // Optional
		bool _default = false; // Optional
		bool _forced = false; // Optional
		ov::String _instream_id; // Required for closed captions
		ov::String _assoc_language; // Optional
		ov::String _uri; // Optional 
		std::shared_ptr<const MediaTrack> _track; // optional, used for audio/video track
	};


	void SetDefaultOptions(bool legacy, bool rewind);

	void SetChunkPath(const ov::String &chunk_path);
	void SetCencProperty(const bmff::CencProperty &cenc_property);

	bool AddMediaCandidateGroup(const std::shared_ptr<const MediaTrackGroup> &track_group, std::function<ov::String(const std::shared_ptr<const MediaTrack> &track)> chunk_uri_generator);
	bool AddMediaCandidate(const LLHlsMasterPlaylist::MediaInfo &media_info);
	bool AddStreamInfo(const ov::String &video_group_id, int video_index_hint, const ov::String &audio_group_id, const ov::String &subtitle_group_id);

	void UpdateCacheForDefaultPlaylist();

	ov::String ToString(const ov::String &chunk_query_string, bool legacy, bool rewind, bool include_path=true) const;
	std::shared_ptr<const ov::Data> ToGzipData(const ov::String &chunk_query_string, bool legacy, bool rewind) const;

private:
	struct MediaGroup
	{
		ov::String _group_id;
		bool _used = false;

		std::shared_ptr<MediaInfo> GetFirstMediaInfo() const
		{
			if (_media_infos.empty())
			{
				return nullptr;
			}

			return _media_infos.front();
		}

		std::vector<std::shared_ptr<MediaInfo>> _media_infos;
	};

	struct StreamInfo
	{
		uint32_t _program_id = 1;
		ov::String _video_group_id; // optional
		ov::String _audio_group_id; // optional
		ov::String _subtitle_group_id; // optional
		ov::String _closed_captions_group_id; // optional, not supported yet in OME
		
		uint32_t _bandwidth = 0;
		uint32_t _width = 0;
		uint32_t _height = 0;
		double _framerate = 0.0;
		ov::String _codecs;

		std::shared_ptr<const MediaInfo> _media_info; // Required
	};

	// Get Media Group
	std::shared_ptr<MediaGroup> GetMediaGroup(const ov::String &group_id) const;

	// Group ID : MediaInfo
	std::map<ov::String, std::shared_ptr<MediaGroup>> _media_groups;
	mutable std::shared_mutex _media_groups_guard;
	std::vector<std::shared_ptr<StreamInfo>> _stream_infos;
	mutable std::shared_mutex _stream_infos_guard;

	ov::String _chunk_path;

	ov::String _cached_default_playlist;
	mutable std::shared_mutex _cached_default_playlist_guard;

	std::shared_ptr<ov::Data> _cached_default_playlist_gzip = nullptr;
	mutable std::shared_mutex _cached_default_playlist_gzip_guard;

	bmff::CencProperty _cenc_property;

	bool _default_legacy = false;
	bool _default_rewind = true;

	ov::String MakePlaylist(const ov::String &chunk_query_string, bool legacy, bool rewind, bool include_path=true) const;
	ov::String MakeSessionKey() const;
};