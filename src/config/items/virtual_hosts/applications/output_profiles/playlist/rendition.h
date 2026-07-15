//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2022 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include "../encodes/audio_profile.h"
#include "../encodes/video_profile.h"

namespace cfg
{
	namespace vhost
	{
		namespace app
		{
			namespace oprf
			{
				struct Rendition : public Item
				{
				protected:
					ov::String _name;
					ov::String _video_name;
					VideoProfile _video_profile;
					bool _video_enabled = false;
					ov::String _audio_name;
					AudioProfile _audio_profile;
					bool _audio_enabled	  = false;

					int _video_index_hint = -1;
					int _audio_index_hint = -1;

				public:
					CFG_DECLARE_CONST_REF_GETTER_OF(GetName, _name);
					CFG_DECLARE_CONST_REF_GETTER_OF(GetVideoName, _video_name);
					CFG_DECLARE_CONST_REF_GETTER_OF(GetAudioName, _audio_name);
					CFG_DECLARE_CONST_REF_GETTER_OF(GetVideoProfile, _video_profile);
					CFG_DECLARE_CONST_REF_GETTER_OF(GetAudioProfile, _audio_profile);
					CFG_DECLARE_CONST_REF_GETTER_OF(IsVideoEnabled, _video_enabled);
					CFG_DECLARE_CONST_REF_GETTER_OF(IsAudioEnabled, _audio_enabled);
					CFG_DECLARE_CONST_REF_GETTER_OF(GetVideoIndexHint, _video_index_hint);
					CFG_DECLARE_CONST_REF_GETTER_OF(GetAudioIndexHint, _audio_index_hint);

					void SetVideoProfile(const VideoProfile &profile)
					{
						_video_profile = profile;
						_video_enabled = true;
					}

					void SetAudioProfile(const AudioProfile &profile)
					{
						_audio_profile = profile;
						_audio_enabled = true;
					}

				protected:
					void MakeList() override
					{
						Register({"Name", "name"}, &_name);
						Register<Optional>({"Video", "video"}, &_video_name);
						Register<Optional>({"Audio", "audio"}, &_audio_name);
						Register<Optional>({"VideoIndexHint", "video_index_hint"}, &_video_index_hint);
						Register<Optional>({"AudioIndexHint", "audio_index_hint"}, &_audio_index_hint);
					}
				};
			}  // namespace oprf
		}  // namespace app
	}  // namespace vhost
}  // namespace cfg