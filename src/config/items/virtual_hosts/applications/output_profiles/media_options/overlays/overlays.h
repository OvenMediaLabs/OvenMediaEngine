//=============================================================================
//
//  OvenMediaEngine
//
//  Created by Keukhan
//  Copyright (c) 2025 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

namespace cfg
{
	namespace vhost
	{
		namespace app
		{
			namespace oprf
			{
				struct Overlays : public Item
				{
				protected:
					bool _enabled = false;
					ov::String _path;

				public:
					CFG_DECLARE_CONST_REF_GETTER_OF(IsEnabled, _enabled)
					CFG_DECLARE_CONST_REF_GETTER_OF(GetPath, _path)

				protected:
					void MakeList() override
					{
						Register<Optional>({"Enable", "enable"}, &_enabled);
						Register<Optional>({"Path", "path"}, &_path);
					}
				};
			}  // namespace oprf
		}  // namespace app
	}  // namespace vhost
}  // namespace cfg