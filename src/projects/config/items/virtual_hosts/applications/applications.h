//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2019 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include "application.h"

namespace cfg
{
	namespace vhost
	{
		namespace app
		{
			struct Applications : public Item
			{
			protected:
				std::vector<Application> _application_list;

			public:
				CFG_DECLARE_REF_GETTER_OF(GetApplicationList, _application_list)
				CFG_DECLARE_CONST_REF_GETTER_OF(GetApplicationList, _application_list)

			protected:
				void MakeList() override
				{
					Register<Optional, OmitJsonName>("Application", &_application_list);
				}
			};
		}  // namespace app
	}  // namespace vhost
}  // namespace cfg
