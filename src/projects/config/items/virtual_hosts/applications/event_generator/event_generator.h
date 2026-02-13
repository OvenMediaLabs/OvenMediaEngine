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
			namespace eg
			{
				struct EventGenerator : public Item
				{
				protected:
					bool _enable	 = false;
					ov::String _path = "";

				public:
					CFG_DECLARE_CONST_REF_GETTER_OF(IsEnable, _enable);
					CFG_DECLARE_CONST_REF_GETTER_OF(GetPath, _path);

				protected:
					void MakeList() override
					{
						Register<Optional>("Enable", &_enable);
						Register<Optional>("Path", &_path);
					}
				};
			}  // namespace eg
		}  // namespace app
	}  // namespace vhost
}  // namespace cfg
