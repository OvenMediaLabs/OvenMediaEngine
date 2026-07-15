//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2025 AirenSoft. All rights reserved.
//
//==============================================================================
#include "api_actions_controller.h"

#include "../../../../api_private.h"
#include "../../../../api_server.h"

namespace api::v1::mgrs::api
{
	void ApiActionsController::PrepareHandlers()
	{
		RegisterPost(R"((reloadCertificate))", &ApiActionsController::OnPostReloadCertificate);
	};

	// POST /v1/managers/api:reloadCertificate
	ApiResponse ApiActionsController::OnPostReloadCertificate(const std::shared_ptr<http::svr::HttpExchange> &client, const Json::Value &request_body)
	{
		auto error = _server->ReloadCertificate();

		if (error != nullptr)
		{
			return error.get();
		}

		return {http::StatusCode::OK};
	}
}  // namespace api::v1::mgrs::api
