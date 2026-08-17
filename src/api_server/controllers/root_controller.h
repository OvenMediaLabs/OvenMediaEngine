//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2020 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include "controller_base.h"

namespace api
{
	class RootController : public ControllerBase<RootController>
	{
	public:
		RootController(const ov::String &access_token);

		void PrepareHandlers() override;

	protected:
		void PrepareAccessTokenHandler();
		ApiResponse OnNotFound(const std::shared_ptr<http::svr::HttpExchange> &client);

		ov::String _access_token;
	};
}  // namespace api
