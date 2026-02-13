//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2024 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <base/ovlibrary/ovlibrary.h>
#include <base/info/media_track.h>
#include <modules/containers/mpegts/mpegts_packager.h>

class HlsMediaPlaylist
{
public:
	struct HlsMediaPlaylistConfig
	{
		uint8_t version = 3;
		size_t segment_count = 10; // It shows the number of segments in the playlist if rewind is disabled
		size_t target_duration = 6;
		bool event_playlist_type = false;
	};

	HlsMediaPlaylist(const ov::String &variant_name, const ov::String &playlist_file_name, const HlsMediaPlaylistConfig &config);

	void AddMediaTrackInfo(const std::shared_ptr<const MediaTrack> &track);

	int64_t GetWallclockOffset() const { return _wallclock_offset_ms; }
	void SetWallclockOffset(int64_t offset_ms) { _wallclock_offset_ms = offset_ms; }

	bool OnSegmentCreated(const std::shared_ptr<base::modules::Segment> &segment);
	bool OnSegmentDeleted(const std::shared_ptr<base::modules::Segment> &segment);

	ov::String GetVariantName() const { return _variant_name; }
	ov::String GetPlaylistFileName() const { return _playlist_file_name; }

	bool HasVideo() const;
	bool HasAudio() const;
	bool HasSubtitle() const;
	std::shared_ptr<const MediaTrack> GetSubtitleTrack() const { return _subtitle_track; }

	uint32_t GetBitrates() const;
	uint32_t GetAverageBitrate() const;
	bool GetResolution(uint32_t &width, uint32_t &height) const;
	// <width>x<height>
	ov::String GetResolutionString() const;
	double GetFramerate() const;
	ov::String GetCodecsString() const;

	ov::String ToString(bool rewind) const;
	ov::String MakeSegmentString(const std::shared_ptr<base::modules::Segment> &segment) const;
	std::shared_ptr<base::modules::Segment> GetLatestSegment() const;

	void SetEndList();

	std::size_t GetSegmentCount() const;

private:
	HlsMediaPlaylistConfig _config;
	ov::String _variant_name;
	ov::String _playlist_file_name;
	
	// Media track ID : Media track
	std::map<uint32_t, std::shared_ptr<const MediaTrack>> _media_tracks;
	std::shared_ptr<const MediaTrack> _first_video_track = nullptr;
	std::shared_ptr<const MediaTrack> _first_audio_track = nullptr;
	std::shared_ptr<const MediaTrack> _subtitle_track = nullptr;	// The subtitle track is used alone in MediaPlaylist

	// Segment number : Segment
	std::map<uint64_t, std::shared_ptr<base::modules::Segment>> _segments;
	mutable std::shared_mutex _segments_mutex;
	int64_t _wallclock_offset_ms = INT64_MIN;

	bool _end_list = false;
};