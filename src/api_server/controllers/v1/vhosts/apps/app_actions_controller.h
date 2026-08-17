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
		class AppActionsController : public ControllerBase<AppActionsController>
		{
		public:
			void PrepareHandlers() override;

		protected:
			// GET /v1/vhosts/<vhost_name>/apps/<app_name>:records
			ApiResponse OnGetRecords(const std::shared_ptr<http::svr::HttpExchange> &client,
									 const std::shared_ptr<mon::HostMetrics> &vhost,
									 const std::shared_ptr<mon::ApplicationMetrics> &app);

			ApiResponse OnPostRecords(const std::shared_ptr<http::svr::HttpExchange> &client, const Json::Value &request_body,
									  const std::shared_ptr<mon::HostMetrics> &vhost,
									  const std::shared_ptr<mon::ApplicationMetrics> &app);

			// POST /v1/vhosts/<vhost_name>/apps/<app_name>:startRecord
			ApiResponse OnPostStartRecord(const std::shared_ptr<http::svr::HttpExchange> &client, const Json::Value &request_body,
										  const std::shared_ptr<mon::HostMetrics> &vhost,
										  const std::shared_ptr<mon::ApplicationMetrics> &app);

			// POST /v1/vhosts/<vhost_name>/apps/<app_name>:stopRecord
			ApiResponse OnPostStopRecord(const std::shared_ptr<http::svr::HttpExchange> &client, const Json::Value &request_body,
										 const std::shared_ptr<mon::HostMetrics> &vhost,
										 const std::shared_ptr<mon::ApplicationMetrics> &app);

			// GET /v1/vhosts/<vhost_name>/apps/<app_name>:pushes
			ApiResponse OnGetPushes(const std::shared_ptr<http::svr::HttpExchange> &client,
									const std::shared_ptr<mon::HostMetrics> &vhost,
									const std::shared_ptr<mon::ApplicationMetrics> &app);

			ApiResponse OnPostPushes(const std::shared_ptr<http::svr::HttpExchange> &client, const Json::Value &request_body,
									 const std::shared_ptr<mon::HostMetrics> &vhost,
									 const std::shared_ptr<mon::ApplicationMetrics> &app);

			// POST /v1/vhosts/<vhost_name>/apps/<app_name>:startPush
			ApiResponse OnPostStartPush(const std::shared_ptr<http::svr::HttpExchange> &client, const Json::Value &request_body,
										const std::shared_ptr<mon::HostMetrics> &vhost,
										const std::shared_ptr<mon::ApplicationMetrics> &app);

			// POST /v1/vhosts/<vhost_name>/apps/<app_name>:stopPush
			ApiResponse OnPostStopPush(const std::shared_ptr<http::svr::HttpExchange> &client, const Json::Value &request_body,
									   const std::shared_ptr<mon::HostMetrics> &vhost,
									   const std::shared_ptr<mon::ApplicationMetrics> &app);

			// GET /v1/vhosts/<vhost_name>/apps/<app_name>:<action>
			ApiResponse OnGetDummyAction(const std::shared_ptr<http::svr::HttpExchange> &client,
										 const std::shared_ptr<mon::HostMetrics> &vhost,
										 const std::shared_ptr<mon::ApplicationMetrics> &app);
		};
	}  // namespace v1
}  // namespace api
