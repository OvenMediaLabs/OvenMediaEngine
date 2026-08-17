//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2023 AirenSoft. All rights reserved.
//
//==============================================================================
#include "vhost_actions_controller.h"

#include <orchestrator/orchestrator.h>

#include <functional>

#include "../../../api_private.h"
#include "publishers/publishers.h"

namespace api
{
	namespace v1
	{
		void VHostActionsController::PrepareHandlers()
		{
			RegisterGet(R"()", &VHostActionsController::OnGetDummyAction);
			RegisterPost(R"((reloadAllCertificates))", &VHostActionsController::OnPostReloadAllCertificates);
			RegisterPost(R"((reloadCertificate))", &VHostActionsController::OnPostReloadCertificate);
		};

		// GET /v1/vhosts:reloadCertificates
		ApiResponse VHostActionsController::OnPostReloadAllCertificates(const std::shared_ptr<http::svr::HttpExchange> &client,
																		const Json::Value &request_body)
		{
			auto orchestrator = ocst::Orchestrator::GetInstance();

			auto result		  = orchestrator->ReloadAllCertificates();
			auto http_code	  = http::StatusCodeFromCommonError(result);

			if (http_code != http::StatusCode::OK)
			{
				throw http::HttpError(http_code, "Failed to reload certificate. Check server log for more information.");
			}

			return {http_code};
		}

		// POST v1/vhosts/<vhost_name>:reloadCertificate
		ApiResponse VHostActionsController::OnPostReloadCertificate(const std::shared_ptr<http::svr::HttpExchange> &client,
																	const Json::Value &request_body,
																	const std::shared_ptr<mon::HostMetrics> &vhost)
		{
			auto orchestrator = ocst::Orchestrator::GetInstance();

			auto result		  = orchestrator->ReloadCertificate(vhost->GetName());
			auto http_code	  = http::StatusCodeFromCommonError(result);

			if (http_code != http::StatusCode::OK)
			{
				throw http::HttpError(http_code, "Failed to reload certificate. Check server log for more information.");
			}

			return {http_code};
		}

		ApiResponse VHostActionsController::OnGetDummyAction(const std::shared_ptr<http::svr::HttpExchange> &client,
															 const std::shared_ptr<mon::HostMetrics> &vhost)
		{
			logte("Called OnGetDummyAction. invoke [%s]", vhost->GetName().CStr());

			return vhost->ToJson();
		}

	}  // namespace v1
}  // namespace api
