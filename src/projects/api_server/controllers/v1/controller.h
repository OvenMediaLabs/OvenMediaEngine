//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2020 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include "../controller_base.h"

namespace api
{
	namespace v1
	{
		class Controller : public ControllerBase<Controller>
		{
		public:
			void PrepareHandlers() override;
			// GET /v1/version
			ApiResponse OnGetVersion(const std::shared_ptr<http::svr::HttpExchange> &client);
		};
	}  // namespace v1
}  // namespace api
