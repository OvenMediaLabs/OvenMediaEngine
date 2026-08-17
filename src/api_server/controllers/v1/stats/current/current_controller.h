//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2020 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include "../../../controller_base.h"

namespace api
{
	namespace v1
	{
		namespace stats
		{
			class CurrentController : public ControllerBase<CurrentController>
			{
			public:
				void PrepareHandlers() override;

				ApiResponse OnGetServerMetrics(const std::shared_ptr<http::svr::HttpExchange> &client);
			};
		}  // namespace stats
	}  // namespace v1
}  // namespace api
