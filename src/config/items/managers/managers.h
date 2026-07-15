//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2020 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include "../common/host/host.h"
#include "api/api.h"

namespace cfg
{
	namespace mgr
	{
		struct Managers : public Item
		{
		protected:
			cmn::Host _host;
			api::API _api;

		public:
			CFG_DECLARE_CONST_REF_GETTER_OF(GetHost, _host)

			CFG_DECLARE_CONST_REF_GETTER_OF(GetApi, _api)

		protected:
			void MakeList() override
			{
				Register("Host", &_host);

				Register<Optional>({"API", "api"}, &_api);
			}
		};
	}  // namespace mgr
}  // namespace cfg