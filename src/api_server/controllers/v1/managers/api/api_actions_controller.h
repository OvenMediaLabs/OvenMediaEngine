//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2025 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include "../../../controller_base.h"

namespace api::v1::mgrs::api
{
	class ApiActionsController : public ControllerBase<ApiActionsController>
	{
	public:
		void PrepareHandlers() override;

	protected:
		// POST /v1/managers/api:reloadCertificate
		ApiResponse OnPostReloadCertificate(const std::shared_ptr<http::svr::HttpExchange> &client, const Json::Value &request_body);
	};
}  // namespace api::v1::mgrs::api
