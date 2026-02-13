//=============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2023 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

namespace cfg
{
	namespace vhost
	{
		namespace app
		{
			namespace pub
			{
				struct Drm : public Item
				{
				protected:
					bool _enabled = false;
					ov::String _drm_info_path;

				public:
					CFG_DECLARE_CONST_REF_GETTER_OF(IsEnabled, _enabled)
					CFG_DECLARE_CONST_REF_GETTER_OF(GetDrmInfoPath, _drm_info_path)

				protected:
					void MakeList() override
					{
						Register<Optional>("Enable", &_enabled);
						Register<Optional>("InfoFile", &_drm_info_path);
					}
				};
			}  // namespace pub
		}  // namespace app
	}  // namespace vhost
}  // namespace cfg
