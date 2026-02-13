//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2020 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <monitoring/monitoring.h>

namespace serdes
{
	Json::Value JsonFromTrack(const std::shared_ptr<const MediaTrack> &tracks);
	Json::Value JsonFromTracks(const std::map<int32_t, std::shared_ptr<MediaTrack>> &tracks);
	Json::Value JsonFromStream(const std::shared_ptr<const mon::StreamMetrics> &stream);
	Json::Value JsonFromStream(const std::shared_ptr<const mon::StreamMetrics> &stream, const std::vector<std::shared_ptr<mon::StreamMetrics>> &output_streams);
	Json::Value JsonFromPlaylist(const std::shared_ptr<info::Playlist> &playlist);
	Json::Value JsonFromPlaylists(const std::map<ov::String, std::shared_ptr<info::Playlist>> &playlists);

}  // namespace serdes