//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2024 AirenSoft. All rights reserved.
//
//==============================================================================

#include "hls_media_playlist.h"
#include "hls_private.h"

#include <algorithm>

#include <base/modules/data_format/cue_event/cue_event.h>

HlsMediaPlaylist::HlsMediaPlaylist(const ov::String &id, const ov::String &playlist_file_name, const HlsMediaPlaylistConfig &config)
	: _config(config)
	, _variant_name(id)
	, _playlist_file_name(playlist_file_name)
{
}

void HlsMediaPlaylist::AddMediaTrackInfo(const std::shared_ptr<const MediaTrack> &track)
{
	std::lock_guard<std::shared_mutex> lock(_tracks_mutex);

	_media_tracks.emplace(track->GetId(), track);

	if (_first_video_track == nullptr && track->GetMediaType() == cmn::MediaType::Video)
	{
		_first_video_track = track;
	}

	if (_first_audio_track == nullptr && track->GetMediaType() == cmn::MediaType::Audio)
	{
		_first_audio_track = track;
	}

	if (_subtitle_track == nullptr && track->GetMediaType() == cmn::MediaType::Subtitle)
	{
		_subtitle_track = track;
	}
}

void HlsMediaPlaylist::UpdateMediaTrackInfo(const std::shared_ptr<const MediaTrack> &track)
{
	std::lock_guard<std::shared_mutex> lock(_tracks_mutex);

	auto it = _media_tracks.find(track->GetId());
	if (it == _media_tracks.end())
	{
		return;
	}
	it->second = track;

	// Refresh the representative track of each type so the master follows the change
	if (_first_video_track != nullptr && _first_video_track->GetId() == track->GetId())
	{
		_first_video_track = track;
	}
	if (_first_audio_track != nullptr && _first_audio_track->GetId() == track->GetId())
	{
		_first_audio_track = track;
	}
	if (_subtitle_track != nullptr && _subtitle_track->GetId() == track->GetId())
	{
		_subtitle_track = track;
	}
}

bool HlsMediaPlaylist::HasTrack(uint32_t track_id) const
{
	std::shared_lock<std::shared_mutex> lock(_tracks_mutex);
	return _media_tracks.find(track_id) != _media_tracks.end();
}

std::shared_ptr<const MediaTrack> HlsMediaPlaylist::GetSubtitleTrack() const
{
	std::shared_lock<std::shared_mutex> lock(_tracks_mutex);
	return _subtitle_track;
}

void HlsMediaPlaylist::SetEndList()
{
	_end_list = true;
}

bool HlsMediaPlaylist::OnSegmentCreated(const std::shared_ptr<base::modules::Segment> &segment)
{
	OV_ASSERT(_wallclock_offset_ms != INT64_MIN, "Wallclock offset is not set");

	std::lock_guard<std::shared_mutex> lock(_segments_mutex);

	logtt("HlsMediaPlaylist::OnSegmentCreated - number(%" PRIu64 ") url(%s) duration_us(%.3f)\n", segment->GetNumber(), segment->GetUrl().CStr(), segment->GetDurationMs());

	if (segment->HasMarker() == true)
	{
		logtd("Marker is found in the segment %" PRIu64 " (%zu)", segment->GetNumber(), segment->GetMarkers().size());
	}

	_segments.emplace(segment->GetNumber(), segment);

	// The codecs union can only grow at a discontinuity; a normal segment repeats
	// the codecs of its generation, which are already covered. A stream that never
	// changes configuration therefore never builds the cache and uses the fallback.
	if (segment->IsDiscontinuityPoint() == true)
	{
		_total_discontinuity_count++;
		RebuildCodecsParameter();
	}

	return true;
}

bool HlsMediaPlaylist::OnSegmentDeleted(const std::shared_ptr<base::modules::Segment> &segment)
{
	std::lock_guard<std::shared_mutex> lock(_segments_mutex);

	logtt("HlsMediaPlaylist::OnSegmentDeleted - number(%" PRId64 ") url(%s) duration_ms(%.3f)\n", segment->GetNumber(), segment->GetUrl().CStr(), segment->GetDurationMs());

	auto it = _segments.find(segment->GetNumber());
	if (it == _segments.end())
	{
		logte("HlsMediaPlaylist::OnSegmentDeleted - Failed to find the segment number %" PRId64 "\n", segment->GetNumber());
		return false;
	}

	// A discontinuity that scrolls out entirely raises the base of the sequence
	if (it->second->IsDiscontinuityPoint() == true)
	{
		_removed_discontinuity_count++;
	}

	uint32_t removed_version = it->second->GetTrackVersion();
	_segments.erase(it);

	// The codecs union can shrink only after the playlist spans more than one version.
	// Segments leave oldest-first and versions are monotonic, so the union changes only
	// when the removed (oldest) version keeps no segment behind.
	if (_total_discontinuity_count > 0)
	{
		bool oldest_version_gone = true;
		if (_segments.empty() == false)
		{
			auto oldest_segment = _segments.begin()->second;
			oldest_version_gone = (oldest_segment->GetTrackVersion() != removed_version);
		}

		if (oldest_version_gone == true)
		{
			RebuildCodecsParameter();
		}
	}

	return true;
}

ov::String HlsMediaPlaylist::ToString(bool rewind) const
{
	std::shared_lock<std::shared_mutex> lock(_segments_mutex);

	ov::String result = "#EXTM3U\n";

	result += ov::String::FormatString("#EXT-X-VERSION:%d\n", 3);
	if (rewind == true && _config.event_playlist_type == true)
	{
		result += ov::String::FormatString("#EXT-X-PLAYLIST-TYPE:EVENT\n");
	}
	result += ov::String::FormatString("#EXT-X-TARGETDURATION:%zu\n", _config.target_duration);

	if (_segments.empty() == true)
	{
		return result;
	}

	std::shared_ptr<base::modules::Segment> first_segment = _segments.begin()->second;
	if (rewind == false)
	{
		size_t segment_size = _segments.size();
		size_t shift_count = segment_size > _config.segment_count ? _config.segment_count : segment_size - 1;
		uint64_t last_segment_number = _segments.rbegin()->second->GetNumber();

		auto it = _segments.find(last_segment_number - shift_count);
		if (it == _segments.end())
		{
			logte("Failed to find the first segment number %" PRIu64 "\n", last_segment_number - shift_count);
			return result;
		}

		first_segment = it->second;
	}

	result += ov::String::FormatString("#EXT-X-MEDIA-SEQUENCE:%" PRIu64 "\n", first_segment->GetNumber());

	// EXT-X-DISCONTINUITY-SEQUENCE counts the discontinuities that precede the first
	// listed segment: those that scrolled out entirely plus those still retained but
	// ahead of the playlist window. The per-segment EXT-X-DISCONTINUITY tags (emitted
	// by MakeSegmentString) cover the rest.
	if (_total_discontinuity_count > 0)
	{
		int64_t discontinuity_sequence = _removed_discontinuity_count;
		for (auto it = _segments.begin(); it != _segments.end() && it->first < first_segment->GetNumber(); ++it)
		{
			if (it->second->IsDiscontinuityPoint() == true)
			{
				discontinuity_sequence++;
			}
		}
		result += ov::String::FormatString("#EXT-X-DISCONTINUITY-SEQUENCE:%" PRId64 "\n", discontinuity_sequence);
	}

	for (auto it = _segments.find(first_segment->GetNumber()); it != _segments.end(); it++)
	{
		const auto &segment = it->second;
		result += MakeSegmentString(segment);
	}

	if (_end_list == true)
	{
		result += "#EXT-X-ENDLIST\n";
	}

	return result;
}

bool HlsMediaPlaylist::HasVideo() const
{
	std::shared_lock<std::shared_mutex> lock(_tracks_mutex);
	return _first_video_track != nullptr;
}

bool HlsMediaPlaylist::HasAudio() const
{
	std::shared_lock<std::shared_mutex> lock(_tracks_mutex);
	return _first_audio_track != nullptr;
}

bool HlsMediaPlaylist::HasSubtitle() const
{
	std::shared_lock<std::shared_mutex> lock(_tracks_mutex);
	return _subtitle_track != nullptr;
}

uint32_t HlsMediaPlaylist::GetBitrates() const
{
	std::shared_lock<std::shared_mutex> lock(_tracks_mutex);
	uint32_t bitrates = 0;
	for (const auto &track_it : _media_tracks)
	{
		const auto &track = track_it.second;
		bitrates += track->GetBitrateLastSecond();
	}

	return bitrates;
}

uint32_t HlsMediaPlaylist::GetAverageBitrate() const
{
	std::shared_lock<std::shared_mutex> lock(_tracks_mutex);
	uint32_t bitrates = 0;
	for (const auto &track_it : _media_tracks)
	{
		const auto &track = track_it.second;

		// conf first, measure next
		bitrates += track->GetBitrate();
	}

	return bitrates;
}

bool HlsMediaPlaylist::GetResolution(uint32_t &width, uint32_t &height) const
{
	std::shared_lock<std::shared_mutex> lock(_tracks_mutex);
	if (_first_video_track == nullptr)
	{
		return false;
	}

	auto resolution = _first_video_track->GetResolution();
	width			= resolution.width;
	height			= resolution.height;

	return true;
}

ov::String HlsMediaPlaylist::GetResolutionString() const
{
	std::shared_lock<std::shared_mutex> lock(_tracks_mutex);
	if (_first_video_track == nullptr)
	{
		return "";
	}

	auto resolution = _first_video_track->GetResolution();
	return ov::String::FormatString("%dx%d", resolution.width, resolution.height);
}

double HlsMediaPlaylist::GetFramerate() const
{
	std::shared_lock<std::shared_mutex> lock(_tracks_mutex);
	if (_first_video_track == nullptr)
	{
		return 0.0;
	}

	return _first_video_track->GetFrameRate();
}

void HlsMediaPlaylist::RebuildCodecsParameter()
{
	// Union of the codecs of every retained segment, so CODECS covers a playlist
	// window that spans a runtime codec change (RFC 8216 6.2.4). Distinct tokens are
	// kept oldest-first. Rebuilt only on add/remove; readers use the cached value.
	std::vector<ov::String> tokens;
	for (const auto &segment_it : _segments)
	{
		auto codecs = segment_it.second->GetCodecsParameter();
		if (codecs.IsEmpty() == true)
		{
			continue;
		}

		for (const auto &token : codecs.Split(","))
		{
			if (token.IsEmpty() == false && std::find(tokens.begin(), tokens.end(), token) == tokens.end())
			{
				tokens.push_back(token);
			}
		}
	}

	ov::String result;
	for (size_t i = 0; i < tokens.size(); i++)
	{
		if (i > 0)
		{
			result += ",";
		}
		result += tokens[i];
	}

	_codecs_parameter = result;
}

ov::String HlsMediaPlaylist::GetCodecsString() const
{
	{
		std::shared_lock<std::shared_mutex> lock(_segments_mutex);
		if (_codecs_parameter.IsEmpty() == false)
		{
			return _codecs_parameter;
		}
	}

	// Fallback before any segment carries codecs (e.g. the very first playlist)
	std::shared_lock<std::shared_mutex> lock(_tracks_mutex);
	ov::String result;
	if (_first_video_track != nullptr)
	{
		result += _first_video_track->GetCodecsParameter();
	}

	if (_first_audio_track != nullptr)
	{
		if (result.IsEmpty() == false)
		{
			result += ",";
		}

		result += _first_audio_track->GetCodecsParameter();
	}

	return result;
}

ov::String HlsMediaPlaylist::MakeSegmentString(const std::shared_ptr<base::modules::Segment> &segment) const
{
	ov::String result;
	if (segment->IsDiscontinuityPoint() == true)
	{
		result += "#EXT-X-DISCONTINUITY\n";
	}
	auto start_time = static_cast<int64_t>(((segment->GetStartTimestamp() * segment->GetTimebaseSeconds()) * 1000.0) + _wallclock_offset_ms);
	std::chrono::system_clock::time_point tp{std::chrono::milliseconds{start_time}};
	result += ov::String::FormatString("#EXT-X-PROGRAM-DATE-TIME:%s\n", ov::Converter::ToISO8601String(tp).CStr());
	result += ov::String::FormatString("#EXTINF:%.3f,\n", segment->GetDurationMs() / 1000.0);
	result += ov::String::FormatString("%s\n", segment->GetUrl().CStr());
	return result;
}

std::shared_ptr<base::modules::Segment> HlsMediaPlaylist::GetLatestSegment() const
{
	std::shared_lock<std::shared_mutex> lock(_segments_mutex);

	if (_segments.empty() == true)
	{
		return nullptr;
	}

	return _segments.rbegin()->second;
}

std::size_t HlsMediaPlaylist::GetSegmentCount() const
{
	std::shared_lock<std::shared_mutex> lock(_segments_mutex);
	return _segments.size();
}
