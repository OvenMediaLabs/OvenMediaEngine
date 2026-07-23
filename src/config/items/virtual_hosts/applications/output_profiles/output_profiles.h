//=============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2020 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include "./decodes/decodes.h"
#include "./hwaccels/hwaccels.h"
#include "./output_profile.h"
#include "./media_options/media_options.h"

namespace cfg
{
	namespace vhost
	{
		namespace app
		{
			namespace oprf
			{
				struct OutputProfiles : public Item
				{
				protected:
					bool _hwaccel = false;
					HWAccels _hwaccels;
					std::vector<OutputProfile> _output_profiles;
					Decodes _decodes;
					MediaOptions _media_options;

				public:
					CFG_DECLARE_CONST_REF_GETTER_OF(IsHardwareAcceleration, _hwaccel);
					CFG_DECLARE_CONST_REF_GETTER_OF(GetHWAccels, _hwaccels);
					CFG_DECLARE_CONST_REF_GETTER_OF(GetOutputProfileList, _output_profiles);
					CFG_DECLARE_CONST_REF_GETTER_OF(GetDecodes, _decodes);
					CFG_DECLARE_CONST_REF_GETTER_OF(GetMediaOptions, _media_options)

				protected:
					void MakeList() override
					{
						// Deprecated
						// Changed to <HWAccels> option.
						Register<Optional>("HardwareAcceleration", &_hwaccel, nullptr,
										   [=]() -> std::shared_ptr<ConfigError> {
											   logw("Config", "The 'HardwareAcceleration' option is deprecated. Please use the 'HWAccels' option.");
											   return nullptr;
										   });
						Register<Optional>({"HWAccels", "hwaccels"}, &_hwaccels);
						Register<Optional>("OutputProfile", &_output_profiles);
						Register<Optional>({"Decodes", "decodes"}, &_decodes);
						Register<Optional>("MediaOptions", &_media_options);
					}
				};
			}  // namespace oprf
		}  // namespace app
	}  // namespace vhost
}  // namespace cfg
