//=============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2025 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include "subtitle_rendition.h"

namespace cfg
{
	namespace vhost
	{
		namespace app
		{
			namespace oprf
			{
				struct Subtitle : public Item
				{
				protected:
					bool _enabled = false; 
					ov::String _default_label;
					int _default_duration_ms = 1000; // default 1 second
					std::vector<SubtitleRendition> _renditions;

				public:
					CFG_DECLARE_CONST_REF_GETTER_OF(IsEnabled, _enabled)
					CFG_DECLARE_CONST_REF_GETTER_OF(GetDefaultLabel, _default_label)
					CFG_DECLARE_CONST_REF_GETTER_OF(GetDefaultDurationMs, _default_duration_ms)
					CFG_DECLARE_REF_GETTER_OF(GetRenditions, _renditions)

				protected:
					void MakeList() override
					{
						Register<Optional>("Enable", &_enabled);
						Register<Optional>("DefaultLabel", &_default_label);
						Register<Optional>("Rendition", &_renditions);
					}
				};
			}  // namespace oprf
		} // namespace app
	} // namespace vhost
}  // namespace cfg
