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
		class AppsController : public ControllerBase<AppsController>
		{
		public:
			void PrepareHandlers() override;

		protected:
			// POST /v1/vhosts/<vhost_name>/apps
			ApiResponse OnPostApp(const std::shared_ptr<http::svr::HttpExchange> &client, const Json::Value &request_body,
								  const std::shared_ptr<mon::HostMetrics> &vhost);

			// GET /v1/vhosts/<vhost_name>/apps
			ApiResponse OnGetAppList(const std::shared_ptr<http::svr::HttpExchange> &client,
									 const std::shared_ptr<mon::HostMetrics> &vhost);

			// GET /v1/vhosts/<vhost_name>/apps/<app_name>
			ApiResponse OnGetApp(const std::shared_ptr<http::svr::HttpExchange> &client,
								 const std::shared_ptr<mon::HostMetrics> &vhost,
								 const std::shared_ptr<mon::ApplicationMetrics> &app);

			// PATCH /v1/vhosts/<vhost_name>/apps/<app_name>
			ApiResponse OnPatchApp(const std::shared_ptr<http::svr::HttpExchange> &client, const Json::Value &request_body,
								   const std::shared_ptr<mon::HostMetrics> &vhost,
								   const std::shared_ptr<mon::ApplicationMetrics> &app);

			// DELETE /v1/vhosts/<vhost_name>/apps/<app_name>
			ApiResponse OnDeleteApp(const std::shared_ptr<http::svr::HttpExchange> &client,
									const std::shared_ptr<mon::HostMetrics> &vhost,
									const std::shared_ptr<mon::ApplicationMetrics> &app);
		};
	}  // namespace v1
}  // namespace api
