//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2022 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

namespace cfg
{
	namespace mgr
	{
		namespace api
		{
			struct Storage : public Item
			{
			protected:
				bool _enabled	 = true;
				ov::String _path = "conf/api_data";

			public:
				CFG_DECLARE_CONST_REF_GETTER_OF(IsEnabled, _enabled)
				CFG_DECLARE_CONST_REF_GETTER_OF(GetPath, _path)

			protected:
				void MakeList() override
				{
					Register<Optional>("Enabled", &_enabled);
					Register<ResolvePath, Optional>("Path", &_path);
				}
			};
		}  // namespace api
	}  // namespace mgr
}  // namespace cfg