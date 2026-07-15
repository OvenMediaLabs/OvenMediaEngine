//=============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2023 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <base/info/audio_map_item.h>

namespace cfg
{
	namespace vhost
	{
		namespace app
		{
			namespace pvd
			{
				struct AudioMapItem : public Item
				{
				protected:
					ov::String _name;
					ov::String _language;
					ov::String _characteristics;
					int _index = -1;

				public:
					CFG_DECLARE_CONST_REF_GETTER_OF(GetName, _name);
					CFG_DECLARE_CONST_REF_GETTER_OF(GetLanguage, _language);
					CFG_DECLARE_CONST_REF_GETTER_OF(GetIndex, _index);
					CFG_DECLARE_CONST_REF_GETTER_OF(GetCharacteristics, _characteristics);

					void SetIndex(int index)
					{
						_index = index;
					}

				protected:
					void MakeList() override
					{
						Register("Name", &_name);
						Register<Optional>("Language", &_language);
						Register<Optional>("Characteristics", &_characteristics);
					}
				};
			}  // namespace pvd
		}  // namespace app
	}  // namespace vhost
}  // namespace cfg
