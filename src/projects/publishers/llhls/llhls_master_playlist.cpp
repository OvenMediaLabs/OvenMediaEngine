//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2022 AirenSoft. All rights reserved.
//
//==============================================================================
#include "llhls_master_playlist.h"

#include <base/ovcrypto/base_64.h>
#include <base/ovlibrary/zip.h>

#include "llhls_private.h"

void LLHlsMasterPlaylist::SetDefaultOptions(bool legacy, bool rewind)
{
	_default_legacy = legacy;
	_default_rewind = rewind;
}

void LLHlsMasterPlaylist::SetChunkPath(const ov::String &chunk_path)
{
	_chunk_path = chunk_path;
}

void LLHlsMasterPlaylist::SetCencProperty(const bmff::CencProperty &cenc_property)
{
	_cenc_property = cenc_property;
}

bool LLHlsMasterPlaylist::AddMediaCandidateGroup(const std::shared_ptr<const MediaTrackGroup> &track_group, std::function<ov::String(const std::shared_ptr<const MediaTrack> &track)> chunk_uri_generator)
{
	auto first_track = track_group->GetFirstTrack();

	auto new_group = std::make_shared<MediaGroup>();
	new_group->_group_id = track_group->GetName();

	// Add media info
	// bool first = true;
	for (auto &track : track_group->GetTracks())
	{
		auto new_media_info = std::make_shared<MediaInfo>();
		new_media_info->_group_id = track_group->GetName();
		new_media_info->_type = track->GetMediaType();
		new_media_info->_name = track->GetPublicName();
		new_media_info->_language = track->GetLanguage();
		new_media_info->_characteristics = track->GetCharacteristics();
		new_media_info->_default = track->IsDefault();;
		new_media_info->_auto_select = track->IsAutoSelect();
		new_media_info->_forced = track->IsForced();
		new_media_info->_instream_id = "";
		new_media_info->_assoc_language = "";
		new_media_info->_uri = chunk_uri_generator(track);
		new_media_info->_track = track;

		new_group->_media_infos.push_back(new_media_info);
		// first = false;
	}

	std::lock_guard<std::shared_mutex> lock(_media_groups_guard);
	_media_groups.emplace(new_group->_group_id, new_group);

	return true;
}

bool LLHlsMasterPlaylist::AddMediaCandidate(const LLHlsMasterPlaylist::MediaInfo &media_info)
{
	auto media_group = GetMediaGroup(media_info._group_id);
	if (media_group == nullptr)
	{
		media_group = std::make_shared<MediaGroup>();
		media_group->_group_id = media_info._group_id;

		std::lock_guard<std::shared_mutex> lock(_media_groups_guard);
		_media_groups.emplace(media_group->_group_id, media_group);
	}
	
	auto new_media_info = std::make_shared<MediaInfo>();
	new_media_info->_group_id = media_info._group_id;
	new_media_info->_type = media_info._type;
	new_media_info->_name = media_info._name;
	new_media_info->_language = media_info._language;
	new_media_info->_characteristics = media_info._characteristics;
	new_media_info->_auto_select = media_info._auto_select;
	new_media_info->_default = media_info._default;
	new_media_info->_forced = media_info._forced;
	new_media_info->_instream_id = media_info._instream_id;
	new_media_info->_assoc_language = media_info._assoc_language;
	new_media_info->_uri = media_info._uri;
	new_media_info->_track = media_info._track;

	std::lock_guard<std::shared_mutex> lock(_media_groups_guard);
	media_group->_media_infos.push_back(new_media_info);

	return true;
}

bool LLHlsMasterPlaylist::AddStreamInfo(const ov::String &video_group_id, int video_index_hint, const ov::String &audio_group_id, const ov::String &subtitle_group_id)
{
	auto new_stream_info = std::make_shared<StreamInfo>();

	// Video/Audio or Video Only
	if (video_group_id.IsEmpty() == false)
	{
		auto video_group = GetMediaGroup(video_group_id);
		if (video_group == nullptr || video_group->_media_infos.size() <= static_cast<std::size_t>(video_index_hint))
		{
			logte("Could not find valid video group: %s", video_group_id.CStr());
			return false;
		}

		// first media info is used for stream info
		new_stream_info->_media_info = video_group->_media_infos[video_index_hint];
		auto video_track = new_stream_info->_media_info->_track;
		if (video_track == nullptr)
		{
			logte("Video track is not valid in first media info of video group: %s", video_group_id.CStr());
			return false;
		}

		new_stream_info->_bandwidth += video_track->GetBitrate();
		auto resolution = video_track->GetResolution();
		new_stream_info->_width = resolution.width;
		new_stream_info->_height = resolution.height;
		new_stream_info->_framerate = video_track->GetFrameRate();
		new_stream_info->_codecs = video_track->GetCodecsParameter();

		// 25-01-27 : Video group is not used now
		// Active media group if video group has more than 1 media info
		// if (video_group->_media_infos.size() > 1)
		// {
		// 	video_group->_used = true;
		// 	new_stream_info->_video_group_id = video_group_id;
		// }

		if (audio_group_id.IsEmpty() == false)
		{
			auto audio_group = GetMediaGroup(audio_group_id);
			if (audio_group == nullptr || audio_group->_media_infos.size() < 1)
			{
				logte("Could not find valid audio group: %s", audio_group_id.CStr());
				return false;
			}

			audio_group->_used = true;
			new_stream_info->_audio_group_id = audio_group_id;

			auto audio_track = audio_group->_media_infos[0]->_track;
			if (audio_track == nullptr)
			{
				logte("Audio track is not valid in first media info of audio group: %s", audio_group_id.CStr());
				return false;
			}

			new_stream_info->_bandwidth += audio_track->GetBitrate();
			new_stream_info->_codecs += ov::String::FormatString(",%s", audio_track->GetCodecsParameter().CStr());
		}

		if (subtitle_group_id.IsEmpty() == false)
		{
			auto subtitle_group = GetMediaGroup(subtitle_group_id);
			if (subtitle_group == nullptr || subtitle_group->_media_infos.size() < 1)
			{
				logte("Could not find valid subtitle group: %s", subtitle_group_id.CStr());
				return false;
			}

			subtitle_group->_used = true;
			new_stream_info->_subtitle_group_id = subtitle_group_id;
		}
	}
	// Audio Only
	else if (audio_group_id.IsEmpty() == false)
	{
		auto audio_group = GetMediaGroup(audio_group_id);
		if (audio_group == nullptr || audio_group->_media_infos.size() < 1)
		{
			logte("Could not find valid audio group: %s", audio_group_id.CStr());
			return false;
		}

		// first media info is used for stream info
		new_stream_info->_media_info = audio_group->_media_infos[0];
		auto audio_track = new_stream_info->_media_info->_track;
		if (audio_track == nullptr)
		{
			logte("Audio track is not valid in first media info of audio group: %s", audio_group_id.CStr());
			return false;
		}

		new_stream_info->_bandwidth += audio_track->GetBitrate();
		new_stream_info->_codecs = audio_track->GetCodecsParameter();

		if (audio_group->_media_infos.size() > 1)
		{
			audio_group->_used = true;
			new_stream_info->_audio_group_id = audio_group_id;
		}

		if (subtitle_group_id.IsEmpty() == false)
		{
			auto subtitle_group = GetMediaGroup(subtitle_group_id);
			if (subtitle_group == nullptr || subtitle_group->_media_infos.size() < 1)
			{
				logte("Could not find valid subtitle group: %s", subtitle_group_id.CStr());
				return false;
			}

			subtitle_group->_used = true;
			new_stream_info->_subtitle_group_id = subtitle_group_id;
		}
	}
	else
	{
		logte("video group id and audio group id are empty");
		return false;
	}

	std::lock_guard<std::shared_mutex> lock(_stream_infos_guard);
	_stream_infos.emplace_back(new_stream_info);

	return true;
}

std::shared_ptr<LLHlsMasterPlaylist::MediaGroup> LLHlsMasterPlaylist::GetMediaGroup(const ov::String &group_id) const
{
	std::shared_lock<std::shared_mutex> lock(_media_groups_guard);

	auto it = _media_groups.find(group_id);
	if (it == _media_groups.end())
	{
		return nullptr;
	}

	return it->second;
}

void LLHlsMasterPlaylist::UpdateCacheForDefaultPlaylist()
{
	// Make default playlist
	// query string is empty
	// legacy is false
	// rewind is true (include all segments)
	// include_path is true
	ov::String playlist = MakePlaylist("", _default_legacy, _default_rewind, true);
	{
		// lock
		std::lock_guard<std::shared_mutex> lock(_cached_default_playlist_guard);
		_cached_default_playlist = playlist;
	}

	{
		// lock
		std::lock_guard<std::shared_mutex> lock(_cached_default_playlist_gzip_guard);
		_cached_default_playlist_gzip = ov::Zip::CompressGzip(playlist.ToData(false));
	}
}

ov::String LLHlsMasterPlaylist::MakeSessionKey() const
{
	ov::String xkey;

	for (const auto &pssh : _cenc_property.pssh_box_list)
	{
		if (pssh.drm_system == bmff::DRMSystem::Widevine)
		{
			xkey.AppendFormat("#EXT-X-SESSION-KEY:");

			if (_cenc_property.scheme == bmff::CencProtectScheme::Cbcs)
			{
				xkey.AppendFormat("METHOD=SAMPLE-AES");
			}
			else if (_cenc_property.scheme == bmff::CencProtectScheme::Cenc)
			{
				xkey.AppendFormat("METHOD=SAMPLE-AES-CTR");
			}

			xkey.AppendFormat(",URI=\"data:text/plain;base64,%s\"", ov::Base64::Encode(pssh.pssh_box_data, false).CStr());

			xkey.AppendFormat(",KEYID=0x%s", _cenc_property.key_id->ToHexString().CStr());
			xkey.AppendFormat(",KEYFORMAT=\"urn:uuid:%s\"", ov::ToUUIDString(pssh.system_id->GetData(), pssh.system_id->GetLength()).LowerCaseString().CStr());
			xkey.AppendFormat(",KEYFORMATVERSIONS=\"1\"");
		}
		else if (pssh.drm_system == bmff::DRMSystem::FairPlay)
		{
			if (_cenc_property.keyformat.LowerCaseString() == "identity")
			{
				xkey.AppendFormat("#EXT-X-SESSION-KEY:METHOD=SAMPLE-AES");
				xkey.AppendFormat(",URI=\"%s\"", _cenc_property.fairplay_key_uri.CStr());
				xkey.AppendFormat(",KEYFORMAT=\"identity\"");
				xkey.AppendFormat(",IV=0x%s", _cenc_property.iv->ToHexString().CStr());
			}
			else
			{
				xkey.AppendFormat("#EXT-X-SESSION-KEY:METHOD=SAMPLE-AES");
				xkey.AppendFormat(",URI=\"%s\"", _cenc_property.fairplay_key_uri.CStr());
				xkey.AppendFormat(",KEYFORMAT=\"com.apple.streamingkeydelivery\"");
				xkey.AppendFormat(",KEYFORMATVERSIONS=\"1\"");
			}
		}

		xkey.Append("\n");
	}

	return xkey;
}

ov::String LLHlsMasterPlaylist::MakePlaylist(const ov::String &chunk_query_string, bool legacy, bool rewind, bool include_path) const
{
	ov::String playlist(10240);

	uint8_t version = 6;
	if (legacy == true)
	{
		version = 6;
	}

	playlist.AppendFormat("#EXTM3U\n");

	// debug info
	// playlist.AppendFormat("#// query_string(%s) legacy(%s) rewind(%s) include_path(%s)\n", chunk_query_string.CStr(), legacy ? "YES" : "NO", rewind ? "YES" : "NO", include_path ? "YES" : "NO");
	playlist.AppendFormat("#EXT-X-VERSION:%d\n", version);
	playlist.AppendFormat("#EXT-X-INDEPENDENT-SEGMENTS\n");

	// Write EXT-X-MEDIA from _media_infos
	// shared lock
	std::shared_lock<std::shared_mutex> media_infos_lock(_media_groups_guard);
	for (const auto &[group_id, media_group] : _media_groups)
	{
		if (media_group->_used == false)
		{
			continue;
		}

		for (const auto &media_info : media_group->_media_infos)
		{
			playlist.AppendFormat("#EXT-X-MEDIA:TYPE=%s", media_info->GetTypeStr().CStr());
			playlist.AppendFormat(",GROUP-ID=\"%s\"", media_info->_group_id.CStr());
			playlist.AppendFormat(",NAME=\"%s\"", media_info->_name.CStr());
			playlist.AppendFormat(",DEFAULT=%s", media_info->_default ? "YES" : "NO");
			playlist.AppendFormat(",AUTOSELECT=%s", media_info->_auto_select ? "YES" : "NO");
			playlist.AppendFormat(",FORCED=%s", media_info->_forced ? "YES" : "NO");

			if (media_info->_type == cmn::MediaType::Audio)
			{
				playlist.AppendFormat(",CHANNELS=\"%d\"", media_info->_track->GetChannel().GetCounts() == 0 ? 2 : media_info->_track->GetChannel().GetCounts());
			}

			if (!media_info->_language.IsEmpty())
			{
				playlist.AppendFormat(",LANGUAGE=\"%s\"", media_info->_language.CStr());
			}

			if (!media_info->_characteristics.IsEmpty())
			{
				playlist.AppendFormat(",CHARACTERISTICS=\"%s\"", media_info->_characteristics.CStr());
			}

			if (!media_info->_instream_id.IsEmpty())
			{
				playlist.AppendFormat(",INSTREAM-ID=\"%s\"", media_info->_instream_id.CStr());
			}

			// _assoc_language
			if (!media_info->_assoc_language.IsEmpty())
			{
				playlist.AppendFormat(",ASSOCIATED-LANGUAGE=\"%s\"", media_info->_assoc_language.CStr());
			}

			if (!media_info->_uri.IsEmpty())
			{
				playlist.AppendFormat(",URI=\"%s%s", include_path ? _chunk_path.CStr() : "", media_info->_uri.CStr());

				ov::String final_query_string;
				if (chunk_query_string.IsEmpty() == false)
				{
					final_query_string.AppendFormat("?%s", chunk_query_string.CStr());
				}

				if (legacy != _default_legacy)
				{
					if (final_query_string.IsEmpty() == false)
					{
						final_query_string.AppendFormat("&_HLS_legacy=%s", legacy ? "YES" : "NO");
					}
					else
					{
						final_query_string.AppendFormat("?_HLS_legacy=%s", legacy ? "YES" : "NO");
					}
				}

				if (rewind != _default_rewind)
				{
					if (final_query_string.IsEmpty() == false)
					{
						final_query_string.AppendFormat("&_HLS_rewind=%s", rewind ? "YES" : "NO");
					}
					else
					{
						final_query_string.AppendFormat("?_HLS_rewind=%s", rewind ? "YES" : "NO");
					}
				}

				if (final_query_string.IsEmpty() == false)
				{
					playlist.AppendFormat("%s", final_query_string.CStr());
				}
			}

			playlist.AppendFormat("\"\n");
		}
	}
	media_infos_lock.unlock();

	playlist.AppendFormat("\n");

	// Write EXT-X-SESSION-KEY
	if (legacy == false)
	{
		playlist.Append(MakeSessionKey());
	}

	playlist.AppendFormat("\n");

	// Write EXT-X-STREAM-INF from _stream_infos
	// shared lock
	std::shared_lock<std::shared_mutex> stream_infos_lock(_stream_infos_guard);
	for (const auto &stream_info : _stream_infos)
	{
		playlist.AppendFormat("#EXT-X-STREAM-INF:");

		// Output Bandwidth
		playlist.AppendFormat("BANDWIDTH=%u", stream_info->_bandwidth == 0 ? 1000000 : stream_info->_bandwidth);

		// If variant_info is video, output RESOLUTION, FRAME-RATE
		if (stream_info->_media_info->_type == cmn::MediaType::Video)
		{
			playlist.AppendFormat(",RESOLUTION=%dx%d", stream_info->_width, stream_info->_height);
			// https://datatracker.ietf.org/doc/html/draft-pantos-hls-rfc8216bis#section-4.4.6.2
			// The value is a decimal-floating-point describing the maximum frame
			// rate for all the video in the Variant Stream, rounded to three
			// decimal places.
			playlist.AppendFormat(",FRAME-RATE=%.3f", stream_info->_framerate == 0 ? 30.0 : stream_info->_framerate);
		}

		// CODECS
		playlist.AppendFormat(",CODECS=\"%s\"", stream_info->_codecs.CStr());

		if (stream_info->_video_group_id.IsEmpty() == false)
		{
			playlist.AppendFormat(",VIDEO=\"%s\"", stream_info->_video_group_id.CStr());
		}

		if (stream_info->_audio_group_id.IsEmpty() == false)
		{
			playlist.AppendFormat(",AUDIO=\"%s\"", stream_info->_audio_group_id.CStr());
		}

		if (stream_info->_subtitle_group_id.IsEmpty() == false)
		{
			playlist.AppendFormat(",SUBTITLES=\"%s\"", stream_info->_subtitle_group_id.CStr());
		}

		// URI
		playlist.AppendFormat("\n");
		playlist.AppendFormat("%s%s", include_path ? _chunk_path.CStr() : "", stream_info->_media_info->_uri.CStr());

		ov::String final_query_string;
		if (chunk_query_string.IsEmpty() == false)
		{
			final_query_string.AppendFormat("?%s", chunk_query_string.CStr());
		}

		if (legacy != _default_legacy)
		{
			if (final_query_string.IsEmpty() == false)
			{
				final_query_string.AppendFormat("&_HLS_legacy=%s", legacy ? "YES" : "NO");
			}
			else
			{
				final_query_string.AppendFormat("?_HLS_legacy=%s", legacy ? "YES" : "NO");
			}
		}

		if (rewind != _default_rewind)
		{
			if (final_query_string.IsEmpty() == false)
			{
				final_query_string.AppendFormat("&_HLS_rewind=%s", rewind ? "YES" : "NO");
			}
			else
			{
				final_query_string.AppendFormat("?_HLS_rewind=%s", rewind ? "YES" : "NO");
			}
		}

		if (final_query_string.IsEmpty() == false)
		{
			playlist.AppendFormat("%s", final_query_string.CStr());
		}

		playlist.AppendFormat("\n");
	}
	stream_infos_lock.unlock();

	return playlist;
}

ov::String LLHlsMasterPlaylist::ToString(const ov::String &chunk_query_string, bool legacy, bool rewind, bool include_path) const
{
	if (chunk_query_string.IsEmpty() && legacy == _default_legacy && rewind == _default_rewind && include_path == true && !_cached_default_playlist.IsEmpty())
	{
		std::shared_lock<std::shared_mutex> lock(_cached_default_playlist_guard);
		return _cached_default_playlist;
	}

	return MakePlaylist(chunk_query_string, legacy, rewind, include_path);
}

std::shared_ptr<const ov::Data> LLHlsMasterPlaylist::ToGzipData(const ov::String &chunk_query_string, bool legacy, bool rewind) const
{
	if (chunk_query_string.IsEmpty() && legacy == _default_legacy && rewind == _default_rewind && _cached_default_playlist_gzip != nullptr)
	{
		std::shared_lock<std::shared_mutex> lock(_cached_default_playlist_gzip_guard);
		return _cached_default_playlist_gzip;
	}

	return ov::Zip::CompressGzip(ToString(chunk_query_string, legacy, rewind).ToData(false));
}