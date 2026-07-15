//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2019 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include "api.h"

namespace cfg
{
	namespace bind
	{
		namespace mgr
		{
			struct Managers : public Item
			{
			protected:
				API _api{"8081/tcp"};

			public:
				CFG_DECLARE_CONST_REF_GETTER_OF(GetApi, _api)

			protected:
				void MakeList() override
				{
					Register<Optional>({"API", "api"}, &_api);
				};
			};
		}  // namespace mgr
	}  // namespace bind
}  // namespace cfg