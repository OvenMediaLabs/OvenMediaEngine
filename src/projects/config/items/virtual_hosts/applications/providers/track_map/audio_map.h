//=============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2023 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include "audio_map_item.h"

namespace cfg
{
	namespace vhost
	{
		namespace app
		{
			namespace pvd
			{
				struct AudioMap : public Item
				{
				protected:
					std::vector<AudioMapItem> _audio_map_items;

				public:
					CFG_DECLARE_CONST_REF_GETTER_OF(GetAudioMapItems, _audio_map_items);

				protected:
					void MakeList() override
					{
						Register("Item", &_audio_map_items, nullptr,
								 [=]() -> std::shared_ptr<ConfigError> {
									 int i = 0;
									 for (auto &audio_map_item : _audio_map_items)
									 {
										 audio_map_item.SetIndex(i);
										 i++;
									 }

									 return nullptr;
								 });
					}
				};
			}  // namespace pvd
		}  // namespace app
	}  // namespace vhost
}  // namespace cfg
