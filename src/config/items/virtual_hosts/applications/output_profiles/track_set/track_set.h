//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2026 OvenMediaLabs. All rights reserved.
//
//==============================================================================
#pragma once

#include <base/info/track_set.h>

#include "track.h"

namespace cfg::vhost::app::oprf
{
	struct TrackSet : public Item
	{
	protected:
		ov::String _name;
		bool _strict = false;
		std::vector<Track> _video_tracks;
		std::vector<Track> _audio_tracks;

	public:
		CFG_DECLARE_CONST_REF_GETTER_OF(GetName, _name);
		CFG_DECLARE_CONST_REF_GETTER_OF(IsStrict, _strict);
		CFG_DECLARE_CONST_REF_GETTER_OF(GetVideoTrackList, _video_tracks);
		CFG_DECLARE_CONST_REF_GETTER_OF(GetAudioTrackList, _audio_tracks);

		// Copy cfg to info
		std::shared_ptr<info::TrackSet> GetTrackSetInfo() const
		{
			auto track_set = std::make_shared<info::TrackSet>(GetName(), IsStrict());

			for (auto &track : _video_tracks)
			{
				auto entry = std::make_shared<info::TrackSetEntry>(track.GetName(), track.GetIndexHint());
				track_set->AddVideoEntry(entry);
			}

			for (auto &track : _audio_tracks)
			{
				auto entry = std::make_shared<info::TrackSetEntry>(track.GetName(), track.GetIndexHint());
				track_set->AddAudioEntry(entry);
			}

			return track_set;
		}

	protected:
		void MakeList() override
		{
			Register("Name", &_name);
			Register<Optional>({"Strict", "strict"}, &_strict);
			Register<Optional>({"Video", "videos"}, &_video_tracks);
			Register<Optional>({"Audio", "audios"}, &_audio_tracks);
		}
	};
}  // namespace cfg::vhost::app::oprf
