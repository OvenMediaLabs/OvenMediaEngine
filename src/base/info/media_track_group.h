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

// Rendition index of the tracks sharing one variant name.
// Concurrency contract: the vector structure is fixed after setup
// (AddTrack/RemoveTrack run before the stream is shared); at runtime only
// ReplaceTrack() swaps slots in place. Slots are accessed atomically.
class MediaTrackGroup
{
public:
	MediaTrackGroup(const ov::String &name);
	~MediaTrackGroup();

	ov::String GetName() const;

	bool AddTrack(const std::shared_ptr<const MediaTrack> &track);

	// Swap the slot of the same track id in place (keeps the group index).
	// Used for generation replacement; safe against concurrent readers.
	bool ReplaceTrack(const std::shared_ptr<const MediaTrack> &track);
	bool RemoveTrack(uint32_t id);

	size_t GetTrackCount() const;
	std::shared_ptr<const MediaTrack> GetFirstTrack() const;
	std::shared_ptr<const MediaTrack> GetTrack(uint32_t order) const;
	// Returns a snapshot; safe to iterate while the owner swaps generations
	std::vector<std::shared_ptr<const MediaTrack>> GetTracks() const;

private:
	ov::String _name;
	std::vector<std::shared_ptr<const MediaTrack>> _tracks;
};