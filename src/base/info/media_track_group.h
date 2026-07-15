//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2023 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include "media_track.h"

class MediaTrackGroup
{
public:
	MediaTrackGroup(const ov::String &name);
	~MediaTrackGroup();

	ov::String GetName() const;

	bool AddTrack(const std::shared_ptr<MediaTrack> &track);
	bool RemoveTrack(uint32_t id);

	size_t GetTrackCount() const;
	std::shared_ptr<MediaTrack> GetFirstTrack() const;
	std::shared_ptr<MediaTrack> GetTrack(uint32_t order) const;
	const std::vector<std::shared_ptr<MediaTrack>> &GetTracks() const;

private:
	ov::String _name;
	std::vector<std::shared_ptr<MediaTrack>> _tracks;
};