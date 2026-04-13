//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2026 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include "stt_rendition.h"

namespace cfg
{
	namespace vhost
	{
		namespace app
		{
			namespace oprf
			{
				struct Stt : public Item
				{
				protected:
					bool _enable = true;
					std::vector<SttRendition> _renditions;

				public:
					CFG_DECLARE_CONST_REF_GETTER_OF(IsEnabled, _enable)
					CFG_DECLARE_CONST_REF_GETTER_OF(GetRenditions, _renditions)

				protected:
					void MakeList() override
					{
						Register<Optional>("Enable", &_enable);
						Register<Optional>("Rendition", &_renditions);
					}
				};
			}  // namespace oprf
		}  // namespace app
	}  // namespace vhost
}  // namespace cfg
