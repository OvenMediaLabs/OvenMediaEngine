//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2024 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include "../../../../controller_base.h"

namespace api
{
	namespace v1
	{
		class MultiplexChannelsController : public ControllerBase<MultiplexChannelsController>
		{
		public:
			void PrepareHandlers() override;

		protected:
			// GET /v1/vhosts/<vhost_name>/apps/<app_name>/streams/
			ApiResponse OnGetChannelList(const std::shared_ptr<http::svr::HttpExchange> &client,
										 const std::shared_ptr<mon::HostMetrics> &vhost,
										 const std::shared_ptr<mon::ApplicationMetrics> &app);

			// POST /v1/vhosts/<vhost_name>/apps/<app_name>
			ApiResponse OnCreateChannel(const std::shared_ptr<http::svr::HttpExchange> &client,
										const Json::Value &request_body,
										const std::shared_ptr<mon::HostMetrics> &vhost,
										const std::shared_ptr<mon::ApplicationMetrics> &app);

			// GET /v1/vhosts/<vhost_name>/apps/<app_name>/streams/<stream_name>
			ApiResponse OnGetChannel(const std::shared_ptr<http::svr::HttpExchange> &client,
									 const std::shared_ptr<mon::HostMetrics> &vhost,
									 const std::shared_ptr<mon::ApplicationMetrics> &app);

			// DELETE /v1/vhosts/<vhost_name>/apps/<app_name>/streams/<stream_name>
			ApiResponse OnDeleteChannel(const std::shared_ptr<http::svr::HttpExchange> &client,
										const std::shared_ptr<mon::HostMetrics> &vhost,
										const std::shared_ptr<mon::ApplicationMetrics> &app);
		};
	}  // namespace v1
}  // namespace api
