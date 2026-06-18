//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2020 AirenSoft. All rights reserved.
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
				struct Encoder : public Item
				{
				protected:
					bool _enable		= false;
					ov::String _modules = "";

				public:
					CFG_DECLARE_CONST_REF_GETTER_OF(IsEnable, _enable);
					CFG_DECLARE_CONST_REF_GETTER_OF(GetModules, _modules);

				protected:
					void MakeList() override
					{
						Register<Optional>("Enable", &_enable, nullptr,
										   [=]() -> std::shared_ptr<ConfigError> {
											   if (_enable == true)
											   {
												   logw("Config", "Hardware acceleration (HWAccels.Encoder) is deprecated. Falling back to the software encoder.");
												   _enable = false;
											   }
											   return nullptr;
										   });
						Register<Optional>("Modules", &_modules);
					}
				};
			}  // namespace oprf
		}  // namespace app
	}  // namespace vhost
}  // namespace cfg