//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2024 AirenSoft. All rights reserved.
//
//==============================================================================

#include "hls_master_playlist.h"

HlsMasterPlaylist::HlsMasterPlaylist(const ov::String &playlist_file_name, const Config &config)
	: _playlist_file_name(playlist_file_name)
	, _config(config)
{
}

bool HlsMasterPlaylist::SetDefaultOption(bool rewind)
{
	_default_option_rewind = rewind;
	return true;
}

void HlsMasterPlaylist::AddMediaPlaylist(const std::shared_ptr<HlsMediaPlaylist> &media_playlist)
{
	std::lock_guard<std::shared_mutex> lock(_media_playlists_mutex);
	_media_playlists.emplace_back(media_playlist);
}

void HlsMasterPlaylist::AddVttPlaylist(const std::shared_ptr<HlsMediaPlaylist> &vtt_playlist)
{
	std::lock_guard<std::shared_mutex> lock(_vtt_playlists_mutex);
	_vtt_playlists.emplace_back(vtt_playlist);
}

ov::String HlsMasterPlaylist::ToString(bool rewind) const
{
	std::vector<std::shared_ptr<HlsMediaPlaylist>> media_playlists;
	{
		// Copy media playlists under lock
		std::shared_lock<std::shared_mutex> lock(_media_playlists_mutex);
		media_playlists = _media_playlists;
	}
	
	std::vector<std::shared_ptr<HlsMediaPlaylist>> vtt_playlists;
	{
		// Copy VTT playlists under lock
		std::shared_lock<std::shared_mutex> lock(_vtt_playlists_mutex);
		vtt_playlists = _vtt_playlists;
	}
	bool has_vtt = vtt_playlists.empty() == false;
	ov::String result = "#EXTM3U\n";
	result += ov::String::FormatString("#EXT-X-VERSION:%d\n\n", _config.version);

	if (has_vtt)
	{
		for (const auto &vtt_playlist : vtt_playlists)
		{
			auto subtitle_track = vtt_playlist->GetSubtitleTrack();
			if (subtitle_track == nullptr)
			{
				continue;
			}

			result.AppendFormat("#EXT-X-MEDIA:TYPE=%s", "SUBTITLES");
			result.AppendFormat(",GROUP-ID=\"%s\"", kSubtitleTrackVariantName); // subtitle group id is fixed
			result.AppendFormat(",NAME=\"%s\"", subtitle_track->GetPublicName().CStr());
			result.AppendFormat(",DEFAULT=%s", subtitle_track->IsDefault() ? "YES" : "NO");
			result.AppendFormat(",AUTOSELECT=%s", subtitle_track->IsAutoSelect() ? "YES" : "NO");
			result.AppendFormat(",FORCED=%s", subtitle_track->IsForced() ? "YES" : "NO");
			result.AppendFormat(",LANGUAGE=\"%s\"", subtitle_track->GetLanguage().CStr());
			if (subtitle_track->GetCharacteristics().IsEmpty() == false)
			{
				result.AppendFormat(",CHARACTERISTICS=\"%s\"", subtitle_track->GetCharacteristics().CStr());
			}
			result.AppendFormat(",URI=\"%s\"\n", vtt_playlist->GetPlaylistFileName().CStr());
		}
	}

	for (const auto &media_playlist : media_playlists)
	{
		if (media_playlist->HasVideo())
		{			
			result += ov::String::FormatString("#EXT-X-STREAM-INF:BANDWIDTH=%d,AVERAGE-BANDWIDTH=%d,RESOLUTION=%s,FRAME-RATE=%.3f,CODECS=\"%s\"",
											media_playlist->GetBitrates(),
											media_playlist->GetAverageBitrate(),
											media_playlist->GetResolutionString().CStr(),
											media_playlist->GetFramerate(),	
											media_playlist->GetCodecsString().CStr());
			if (has_vtt)
			{
				result += ov::String::FormatString(",SUBTITLES=\"%s\"", kSubtitleTrackVariantName);
			}
			result += "\n";
											
		}
		else
		{
			result += ov::String::FormatString("#EXT-X-STREAM-INF:BANDWIDTH=%d,AVERAGE-BANDWIDTH=%d,CODECS=\"%s\"",
											media_playlist->GetBitrates(),
											media_playlist->GetAverageBitrate(),
											media_playlist->GetCodecsString().CStr());
			if (has_vtt)
			{
				result += ov::String::FormatString(",SUBTITLES=\"%s\"", kSubtitleTrackVariantName);
			}
			result += "\n";
		}

		result += ov::String::FormatString("%s", media_playlist->GetPlaylistFileName().CStr());

		if (rewind != _default_option_rewind)
		{
			result += ov::String::FormatString("?_HLS_rewind=%s", rewind ? "YES" : "NO");
		}

		result += "\n";
	}

	return result;
}