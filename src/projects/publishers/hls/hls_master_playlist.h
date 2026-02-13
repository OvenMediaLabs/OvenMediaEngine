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

#include "hls_media_playlist.h"

class HlsMasterPlaylist
{
public:
	struct Config
	{
		uint8_t version = 3;
	};

	HlsMasterPlaylist(const ov::String &playlist_file_name, const Config &config);

	bool SetDefaultOption(bool rewind);

	ov::String GetFileName() const { return _playlist_file_name; }
	
	void AddMediaPlaylist(const std::shared_ptr<HlsMediaPlaylist> &media_playlist);
	void AddVttPlaylist(const std::shared_ptr<HlsMediaPlaylist> &vtt_playlist);

	ov::String ToString(bool rewind) const;

private:
	ov::String _playlist_file_name;
	Config _config;

	bool _default_option_rewind = true;

	std::vector<std::shared_ptr<HlsMediaPlaylist>> _media_playlists;
	mutable std::shared_mutex _media_playlists_mutex;

	std::vector<std::shared_ptr<HlsMediaPlaylist>> _vtt_playlists;
	mutable std::shared_mutex _vtt_playlists_mutex;
};