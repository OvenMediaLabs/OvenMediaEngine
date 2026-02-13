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
		std::shared_ptr<const RtcRendition> next_rendition = nullptr;
		uint64_t base_bitrates							   = base_rendition->GetBitrates();

		for (const auto &[name, rendition] : _rendition_map)
		{
			if (rendition == base_rendition)
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
		std::shared_ptr<const RtcRendition> next_rendition = nullptr;
		uint64_t base_bitrates							   = base_rendition->GetBitrates();

		for (const auto &[name, rendition] : _rendition_map)
		{
			if (rendition == base_rendition)
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

	bool AddRendition(const std::shared_ptr<const RtcRendition> &rendition)
	{
		auto video_codec_id = rendition->GetVideoTrack() ? rendition->GetVideoTrack()->GetCodecId() : cmn::MediaCodecId::None;
		auto audio_codec_id = rendition->GetAudioTrack() ? rendition->GetAudioTrack()->GetCodecId() : cmn::MediaCodecId::None;

		auto playlist		= GetPlaylist(video_codec_id, audio_codec_id);
		if (playlist == nullptr)
		{
			playlist = CreatePlaylist(video_codec_id, audio_codec_id);

			// Options
			playlist->SetWebRtcAutoAbr(_webrtc_auto_abr);

			_playlist_map.emplace(GetPlaylistKey(video_codec_id, audio_codec_id), playlist);
		}

		playlist->AddRendition(rendition);

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

	// Track list for payload list
	// OME specifies only one payload per codec in WebRTC SDP.
	std::map<cmn::MediaCodecId, std::shared_ptr<const MediaTrack>> _payload_track_map;

	// Key : string(%d_%d), video_codec_id, audio_codec_id
	// Value : Rendition list
	std::map<ov::String, std::shared_ptr<RtcPlaylist>> _playlist_map;
};
