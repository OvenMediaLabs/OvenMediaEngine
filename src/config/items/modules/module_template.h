//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2022 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

namespace cfg
{
	namespace modules
	{
		struct ModuleTemplate : public Item
		{
		protected:
			bool _enable;

		public:
			ModuleTemplate(bool enable)
				: _enable(enable)
			{
			}

			CFG_DECLARE_CONST_REF_GETTER_OF(IsEnabled, _enable)

			void SetEnable(bool enable)
			{
				_enable = enable;
			}

		protected:
			void MakeList() override
			{
				Register<Optional>({"Enable"}, &_enable);
			}
		};
	}  // namespace modules
}  // namespace cfg
