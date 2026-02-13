//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2023 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include "../../controller_base.h"

namespace api
{
	namespace v1
	{
		class VHostActionsController : public ControllerBase<VHostActionsController>
		{
		public:
			void PrepareHandlers() override;

		protected:
			// GET /v1/vhosts:reloadCertificates
			ApiResponse OnPostReloadAllCertificates(const std::shared_ptr<http::svr::HttpExchange> &client,
													const Json::Value &request_body);

			// POST v1/vhosts/<vhost_name>:reloadCertificate
			ApiResponse OnPostReloadCertificate(const std::shared_ptr<http::svr::HttpExchange> &client,
												const Json::Value &request_body,
												const std::shared_ptr<mon::HostMetrics> &vhost);

			// GET /v1/vhosts/<vhost_name>:<action>
			ApiResponse OnGetDummyAction(const std::shared_ptr<http::svr::HttpExchange> &client,
										 const std::shared_ptr<mon::HostMetrics> &vhost);
		};
	}  // namespace v1
}  // namespace api
