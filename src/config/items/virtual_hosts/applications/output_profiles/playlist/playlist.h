//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2022 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <base/info/playlist.h>

#include "../encodes/encodes.h"
#include "options.h"
#include "rendition.h"
#include "rendition_template.h"

namespace cfg
{
	namespace vhost
	{
		namespace app
		{
			namespace oprf
			{
				struct Playlist : public Item
				{
				protected:
					ov::String _name;
					ov::String _file_name;
					Options _options;
					std::vector<Rendition> _renditions;
					std::vector<RenditionTemplate> _rendition_templates;

				public:
					CFG_DECLARE_CONST_REF_GETTER_OF(GetName, _name);
					CFG_DECLARE_CONST_REF_GETTER_OF(GetFileName, _file_name);
					CFG_DECLARE_CONST_REF_GETTER_OF(GetOptions, _options);
					CFG_DECLARE_CONST_REF_GETTER_OF(GetRenditions, _renditions);
					CFG_DECLARE_CONST_REF_GETTER_OF(GetRenditionTemplates, _rendition_templates);

					// Copy cfg to info
					std::shared_ptr<info::Playlist> GetPlaylistInfo() const
					{
						auto playlist = std::make_shared<info::Playlist>(GetName(), GetFileName(), false);
						playlist->SetHlsChunklistPathDepth(_options.GetHlsChunklistPathDepth());
						playlist->SetWebRtcAutoAbr(_options.IsWebRtcAutoAbr());
						playlist->EnableTsPackaging(_options.IsTsPackagingEnabled());
						playlist->EnableSubtitles(_options.IsSubtitlesEnabled());

						for (auto &rendition : _renditions)
						{
							auto rendition_info = std::make_shared<info::Rendition>(rendition.GetName(), rendition.GetVideoName(), rendition.GetAudioName());
							rendition_info->SetVideoIndexHint(rendition.GetVideoIndexHint());
							rendition_info->SetAudioIndexHint(rendition.GetAudioIndexHint());

							playlist->AddRendition(rendition_info);
						}

						return playlist;
					}

					bool SetEncodes(const Encodes &encodes)
					{
						bool video_found = false;
						bool audio_found = false;
						for (auto &rendition : _renditions)
						{
							video_found		   = false;
							audio_found		   = false;

							bool video_enabled = false;
							auto video_name	   = rendition.GetVideoName(&video_enabled);

							if (video_enabled)
							{
								for (const auto &encode : encodes.GetVideoProfileList())
								{
									if (video_name == encode.GetName())
									{
										rendition.SetVideoProfile(encode);
										video_found = true;
									}
								}
							}

							bool audio_enabled = false;
							auto audio_name	   = rendition.GetAudioName(&audio_enabled);

							if (audio_enabled)
							{
								for (const auto &encode : encodes.GetAudioProfileList())
								{
									if (audio_name == encode.GetName())
									{
										rendition.SetAudioProfile(encode);
										audio_found = true;
									}
								}
							}

							bool result = true;
							if (!video_found && video_enabled)
							{
								loge("Config", "[Renditions] Video profile not found: %s", video_name.CStr());
								result = false;
							}

							if (!audio_found && audio_enabled)
							{
								loge("Config", "[Renditions] Audio profile not found: %s", audio_name.CStr());
								result = false;
							}

							if (result == false)
							{
								return false;
							}
						}

						return true;
					}

				protected:
					void MakeList() override
					{
						Register<Optional>("Name", &_name);
						Register("FileName", &_file_name);
						Register<Optional>("Options", &_options);

						Register<Optional>({"Rendition", "renditions"}, &_renditions, nullptr,
										   [=]() -> std::shared_ptr<ConfigError> {
											   std::map<ov::String, bool> rendition_names;

											   // Check if there are duplicate renditions
											   for (auto &rendition : _renditions)
											   {
												   auto name = rendition.GetName();
												   if (rendition_names.find(name) != rendition_names.end())
												   {
													   return CreateConfigErrorPtr("Duplicate rendition name: %s", name.CStr());
												   }
												   rendition_names[name] = true;
											   }

											   return nullptr;
										   });

						Register<Optional>({"RenditionTemplate", "rendition_templates"}, &_rendition_templates);
					}
				};
			}  // namespace oprf
		}  // namespace app
	}  // namespace vhost
}  // namespace cfg