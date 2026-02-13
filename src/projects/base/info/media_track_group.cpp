//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2023 AirenSoft. All rights reserved.
//
//==============================================================================
#include "media_track_group.h"

MediaTrackGroup::MediaTrackGroup(const ov::String &name)
{
	_name = name;
}

MediaTrackGroup::~MediaTrackGroup()
{
}

ov::String MediaTrackGroup::GetName() const
{
	return _name;
}

bool MediaTrackGroup::AddTrack(const std::shared_ptr<MediaTrack> &track)
{
	if (track == nullptr)
	{
		return false;
	}

	track->SetGroupIndex(_tracks.size());
	_tracks.push_back(track);

	return true;
}

bool MediaTrackGroup::RemoveTrack(uint32_t id)
{
	auto it = std::find_if(_tracks.begin(), _tracks.end(), [id](const std::shared_ptr<MediaTrack> &t) {
		return t->GetId() == id;
	});

	if (it == _tracks.end())
	{
		return false;
	}
	
	_tracks.erase(it);

	// Update group index
	for (size_t i = 0; i < _tracks.size(); i++)
	{
		_tracks[i]->SetGroupIndex(i);
	}

	return true;
}

size_t MediaTrackGroup::GetTrackCount() const
{
	return _tracks.size();
}

std::shared_ptr<MediaTrack> MediaTrackGroup::GetFirstTrack() const
{
	return GetTrack(0);
}

std::shared_ptr<MediaTrack> MediaTrackGroup::GetTrack(uint32_t order) const
{
	if (order >= _tracks.size())
	{
		return nullptr;
	}

	return _tracks[order];
}

const std::vector<std::shared_ptr<MediaTrack>> &MediaTrackGroup::GetTracks() const
{
	return _tracks;
}