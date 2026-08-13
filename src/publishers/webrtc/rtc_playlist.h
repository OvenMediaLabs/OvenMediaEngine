//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2022 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <vector>

#include <base/common_types.h>
#include <base/info/media_track.h>
#include <modules/json_serdes/stream.h>

class RtcRendition
{
public:
	RtcRendition(const ov::String &name, const std::shared_ptr<const MediaTrack> &video_track, const std::shared_ptr<const MediaTrack> &audio_track)
	{
		_name		 = name;
		_video_track = video_track;
		_audio_track = audio_track;
	}

	const ov::String &GetName() const
	{
		return _name;
	}
	const std::shared_ptr<const MediaTrack> &GetVideoTrack() const
	{
		return _video_track;
	}
	const std::shared_ptr<const MediaTrack> &GetAudioTrack() const
	{
		return _audio_track;
	}

	// Get bitrates
	uint64_t GetBitrates() const
	{
		uint64_t bitrates = 0;

		if (_video_track != nullptr)
		{
			bitrates += _video_track->GetBitrate();
		}

		if (_audio_track != nullptr)
		{
			bitrates += _audio_track->GetBitrate();
		}

		return bitrates;
	}

	Json::Value ToJson() const
	{
		Json::Value json;

		json["name"] = _name.CStr();

		if (_video_track)
		{
			auto video_track_json = serdes::JsonFromTrack(_video_track);
			json["video_track"]	  = video_track_json;
		}

		if (_audio_track)
		{
			auto audio_track_json = serdes::JsonFromTrack(_audio_track);
			json["audio_track"]	  = audio_track_json;
		}

		return json;
	}

private:
	ov::String _name;
	std::shared_ptr<const MediaTrack> _video_track;
	std::shared_ptr<const MediaTrack> _audio_track;
};

// Rendition list with same video/audio tracks
class RtcPlaylist
{
public:
	RtcPlaylist(const ov::String &name, const ov::String &file_name, cmn::MediaCodecId video_codec_id, cmn::MediaCodecId audio_codec_id)
	{
		_name			= name;
		_file_name		= file_name;
		_video_codec_id = video_codec_id;
		_audio_codec_id = audio_codec_id;
	}

	// Expose the codecs this per-session playlist was built for, so the master
	// playlist can cross-add audio-only renditions into matching video playlists.
	cmn::MediaCodecId GetVideoCodecId() const { return _video_codec_id; }
	cmn::MediaCodecId GetAudioCodecId() const { return _audio_codec_id; }

	void SetWebRtcAutoAbr(bool auto_abr)
	{
		_webrtc_auto_abr = auto_abr;
	}

	bool IsWebRtcAutoAbr() const
	{
		return _webrtc_auto_abr;
	}

	bool AddRendition(const std::shared_ptr<const RtcRendition> &rendition)
	{
		if (_first_rendition == nullptr)
		{
			_first_rendition = rendition;
		}

		_rendition_map.emplace(rendition->GetName(), rendition);

		return true;
	}

	// Get Rendition Map
	const std::map<ov::String, std::shared_ptr<const RtcRendition>> &GetRenditions() const
	{
		return _rendition_map;
	}

	// Get next higher bitrates rendition
	std::shared_ptr<const RtcRendition> GetNextHigherBitrateRendition(const std::shared_ptr<const RtcRendition> &base_rendition) const
	{
		// A session on an audio-only rendition inside a video playlist was put there by an
		// explicit change_rendition; automatic ABR must keep it there (never auto-resume
		// video) until the client explicitly switches back. An audio-only playlist
		// (_video_codec_id == None) keeps normal bitrate ABR among its renditions.
		if (_video_codec_id != cmn::MediaCodecId::None && base_rendition->GetVideoTrack() == nullptr)
		{
			return nullptr;
		}

		std::shared_ptr<const RtcRendition> next_rendition = nullptr;
		uint64_t base_bitrates = base_rendition->GetBitrates();

		for (const auto &[name, rendition] : _rendition_map)
		{
			if (rendition == base_rendition)
			{
				continue;
			}

			// Never auto-select an audio-only rendition as an ABR destination in a video
			// playlist (an audio-only playlist keeps normal bitrate ABR among its renditions).
			if (_video_codec_id != cmn::MediaCodecId::None && rendition->GetVideoTrack() == nullptr)
			{
				continue;
			}

			auto bitrate = rendition->GetBitrates();
			if (bitrate > base_bitrates)
			{
				if (next_rendition == nullptr)
				{
					next_rendition = rendition;
				}
				else if (next_rendition != nullptr && next_rendition->GetBitrates() > bitrate)
				{
					next_rendition = rendition;
				}
			}
		}

		return next_rendition;
	}

	// Get next lower bitrates rendition
	std::shared_ptr<const RtcRendition> GetNextLowerBitrateRendition(const std::shared_ptr<const RtcRendition> &base_rendition) const
	{
		// See GetNextHigherBitrateRendition: a video session that explicitly switched to an
		// audio-only rendition stays there; automatic ABR never moves it. An audio-only
		// playlist (_video_codec_id == None) keeps normal bitrate ABR among its renditions.
		if (_video_codec_id != cmn::MediaCodecId::None && base_rendition->GetVideoTrack() == nullptr)
		{
			return nullptr;
		}

		std::shared_ptr<const RtcRendition> next_rendition = nullptr;
		uint64_t base_bitrates							   = base_rendition->GetBitrates();

		for (const auto &[name, rendition] : _rendition_map)
		{
			if (rendition == base_rendition)
			{
				continue;
			}

			// Never auto-select an audio-only rendition as an ABR destination in a video
			// playlist (an audio-only playlist keeps normal bitrate ABR among its renditions).
			if (_video_codec_id != cmn::MediaCodecId::None && rendition->GetVideoTrack() == nullptr)
			{
				continue;
			}

			auto bitrate = rendition->GetBitrates();
			if (bitrate < base_bitrates)
			{
				if (next_rendition == nullptr)
				{
					next_rendition = rendition;
				}
				else if (next_rendition != nullptr && next_rendition->GetBitrates() < bitrate)
				{
					next_rendition = rendition;
				}
			}
		}

		return next_rendition;
	}

	std::shared_ptr<const RtcRendition> GetRendition(const ov::String &name) const
	{
		auto it = _rendition_map.find(name);
		if (it == _rendition_map.end())
		{
			return nullptr;
		}

		return it->second;
	}

	std::shared_ptr<const RtcRendition> GetFirstRendition() const
	{
		return _first_rendition;
	}

	Json::Value ToJson(bool auto_abr = false) const
	{
		Json::Value playlist_json;

		playlist_json["name"]	   = _name.CStr();
		playlist_json["file_name"] = _file_name.CStr();
		playlist_json["auto"]	   = auto_abr;

		Json::Value renditions_json;
		for (const auto &it : _rendition_map)
		{
			renditions_json.append(it.second->ToJson());
		}

		playlist_json["renditions"] = renditions_json;

		return playlist_json;
	}

private:
	ov::String _name;
	ov::String _file_name;
	bool _webrtc_auto_abr = false;
	cmn::MediaCodecId _video_codec_id;
	cmn::MediaCodecId _audio_codec_id;

	std::shared_ptr<const RtcRendition> _first_rendition;
	// Rendition Name : Rendition
	std::map<ov::String, std::shared_ptr<const RtcRendition>> _rendition_map;
};

class RtcMasterPlaylist
{
public:
	RtcMasterPlaylist(const ov::String &name, const ov::String &file_name)
	{
		_name	   = name;
		_file_name = file_name;
	}

	void SetWebRtcAutoAbr(bool auto_abr)
	{
		_webrtc_auto_abr = auto_abr;
	}

	bool IsWebRtcAutoAbr() const
	{
		return _webrtc_auto_abr;
	}

	// When enabled, audio-only renditions are cross-added into video playlists that share
	// their audio codec (see AddRendition), so a video session can switch to audio-only.
	void SetWebRtcAudioOnlyFallback(bool enabled)
	{
		_webrtc_audio_only_fallback = enabled;
	}

	bool IsWebRtcAudioOnlyFallback() const
	{
		return _webrtc_audio_only_fallback;
	}

	bool AddRendition(const std::shared_ptr<const RtcRendition> &rendition)
	{
		auto video_codec_id = rendition->GetVideoTrack() ? rendition->GetVideoTrack()->GetCodecId() : cmn::MediaCodecId::None;
		auto audio_codec_id = rendition->GetAudioTrack() ? rendition->GetAudioTrack()->GetCodecId() : cmn::MediaCodecId::None;

		// An audio-only rendition (no video track) is otherwise bucketed into its own
		// (None, audio) playlist, so a session that negotiated video never sees it and
		// cannot switch down to audio-only on the SAME PeerConnection. When the feature is
		// enabled, expose audio-only renditions inside every existing video playlist that
		// shares the same audio codec.
		if (_webrtc_audio_only_fallback && video_codec_id == cmn::MediaCodecId::None && audio_codec_id != cmn::MediaCodecId::None)
		{
			_audio_only_renditions.push_back(rendition);
			for (auto &[key, pl] : _playlist_map)
			{
				// Match on audio codec id only: WebRTC advertises a single payload type per
				// codec, so the audio-only rendition's packets ride the video playlist's
				// already-negotiated audio payload type even though it is a different track.
				if (pl->GetVideoCodecId() != cmn::MediaCodecId::None && pl->GetAudioCodecId() == audio_codec_id)
				{
					pl->AddRendition(rendition);
				}
			}
		}

		auto playlist		  = GetPlaylist(video_codec_id, audio_codec_id);
		bool created_playlist = (playlist == nullptr);
		if (created_playlist)
		{
			playlist = CreatePlaylist(video_codec_id, audio_codec_id);

			// Options
			playlist->SetWebRtcAutoAbr(_webrtc_auto_abr);

			_playlist_map.emplace(GetPlaylistKey(video_codec_id, audio_codec_id), playlist);
		}

		// Add the triggering rendition first so it stays the playlist's default
		// rendition (GetFirstRendition), before any cross-added audio-only rendition.
		playlist->AddRendition(rendition);

		// Back-fill a newly-created video playlist with audio-only renditions already
		// seen that share its audio codec (handles an audio-only rendition declared
		// before the video one). Done after the video rendition is added so the video
		// rendition - not the audio-only one - remains the default.
		if (_webrtc_audio_only_fallback && created_playlist && video_codec_id != cmn::MediaCodecId::None)
		{
			for (const auto &ao : _audio_only_renditions)
			{
				auto ao_audio = ao->GetAudioTrack() ? ao->GetAudioTrack()->GetCodecId() : cmn::MediaCodecId::None;
				if (ao_audio == audio_codec_id)
				{
					playlist->AddRendition(ao);
				}
			}
		}

		AddPayloadTrack(rendition->GetVideoTrack());
		AddPayloadTrack(rendition->GetAudioTrack());

		return true;
	}

	// Track List for WebRTC Payload
	std::map<cmn::MediaCodecId, std::shared_ptr<const MediaTrack>> GetPayloadTrackMap() const
	{
		return _payload_track_map;
	}

	// Get Playlist
	std::shared_ptr<const RtcPlaylist> GetPlaylist(cmn::MediaCodecId video_codec_id, cmn::MediaCodecId audio_codec_id) const
	{
		auto it = _playlist_map.find(GetPlaylistKey(video_codec_id, audio_codec_id));
		if (it == _playlist_map.end())
		{
			return nullptr;
		}

		auto playlist = it->second;

		return playlist;
	}

	std::shared_ptr<RtcPlaylist> GetPlaylist(cmn::MediaCodecId video_codec_id, cmn::MediaCodecId audio_codec_id)
	{
		auto it = _playlist_map.find(GetPlaylistKey(video_codec_id, audio_codec_id));
		if (it == _playlist_map.end())
		{
			return nullptr;
		}

		return it->second;
	}

private:
	std::shared_ptr<RtcPlaylist> CreatePlaylist(cmn::MediaCodecId video_codec_id, cmn::MediaCodecId audio_codec_id) const
	{
		return std::make_shared<RtcPlaylist>(_name, _file_name, video_codec_id, audio_codec_id);
	}

	ov::String GetPlaylistKey(cmn::MediaCodecId video_codec_id, cmn::MediaCodecId audio_codec_id) const
	{
		return ov::String::FormatString("%u_%u", ov::ToUnderlyingType(video_codec_id), ov::ToUnderlyingType(audio_codec_id));
	}

	void AddPayloadTrack(const std::shared_ptr<const MediaTrack> &track)
	{
		if (track == nullptr)
		{
			return;
		}

		_payload_track_map.emplace(track->GetCodecId(), track);
	}

	ov::String _name;
	ov::String _file_name;
	bool _webrtc_auto_abr = false;
	bool _webrtc_audio_only_fallback = false;

	// Track list for payload list
	// OME specifies only one payload per codec in WebRTC SDP.
	std::map<cmn::MediaCodecId, std::shared_ptr<const MediaTrack>> _payload_track_map;

	// Key : string(%d_%d), video_codec_id, audio_codec_id
	// Value : Rendition list
	std::map<ov::String, std::shared_ptr<RtcPlaylist>> _playlist_map;

	// Audio-only renditions, cross-added into matching video playlists (see AddRendition).
	std::vector<std::shared_ptr<const RtcRendition>> _audio_only_renditions;
};
