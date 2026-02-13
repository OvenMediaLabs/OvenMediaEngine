//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2020 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include "audio_profile.h"
#include "image_profile.h"
#include "video_profile.h"
#include "speech_to_text_profile.h"

namespace cfg
{
	namespace vhost
	{
		namespace app
		{
			namespace oprf
			{
				struct Encodes : public Item
				{
				protected:
					std::vector<AudioProfile> _audio_profiles;
					std::vector<VideoProfile> _video_profiles;
					std::vector<ImageProfile> _image_profiles;
					std::vector<SpeechToTextProfile> _speech_to_text_profiles;

				public:
					CFG_DECLARE_CONST_REF_GETTER_OF(GetAudioProfileList, _audio_profiles);
					CFG_DECLARE_CONST_REF_GETTER_OF(GetVideoProfileList, _video_profiles);
					CFG_DECLARE_CONST_REF_GETTER_OF(GetImageProfileList, _image_profiles);
					CFG_DECLARE_CONST_REF_GETTER_OF(GetSpeechToTextProfileList, _speech_to_text_profiles);

					void AddSpeechToTextProfiles(const SpeechToTextProfile &profiles)
					{
						_speech_to_text_profiles.push_back(profiles);
					}

				protected:
					void MakeList() override
					{
						Register<Optional>({"Audio", "audios"}, &_audio_profiles, [=]() -> std::shared_ptr<ConfigError> { return nullptr; }, [=]() -> std::shared_ptr<ConfigError> {
							
							uint32_t index = 0;
							for (auto &profile : _audio_profiles)
							{
								if (profile.GetName().IsEmpty())
								{
									profile.SetName(ov::String::FormatString("audio_%d", index));
									index++;
								}
							}

							return nullptr; });

						Register<Optional>({"Video", "videos"}, &_video_profiles, [=]() -> std::shared_ptr<ConfigError> { return nullptr; }, [=]() -> std::shared_ptr<ConfigError> {
							uint32_t index = 0;
							for (auto &profile : _video_profiles)
							{
								if (profile.GetName().IsEmpty())
								{
									profile.SetName(ov::String::FormatString("video_%d", index));
									index++;
								}
							}

							return nullptr; });
						Register<Optional>({"Image", "images"}, &_image_profiles, [=]() -> std::shared_ptr<ConfigError> { return nullptr; }, [=]() -> std::shared_ptr<ConfigError> {
							uint32_t index = 0;
							for (auto &profile : _image_profiles)
							{
								if (profile.GetName().IsEmpty())
								{
									profile.SetName(ov::String::FormatString("image_%d", index));
									index++;
								}
							}

							return nullptr; });
					}
				};
			}  // namespace oprf
		}  // namespace app
	}  // namespace vhost
}  // namespace cfg