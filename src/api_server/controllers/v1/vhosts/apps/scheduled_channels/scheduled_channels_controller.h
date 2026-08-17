//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2023 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include "../../../../controller_base.h"

namespace api
{
	namespace v1
	{
		class ScheduledChannelsController : public ControllerBase<ScheduledChannelsController>
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

			// PATCH /v1/vhosts/<vhost_name>/apps/<app_name>/streams/<stream_name>
			ApiResponse OnPatchChannel(const std::shared_ptr<http::svr::HttpExchange> &client,
									   const Json::Value &request_body,
									   const std::shared_ptr<mon::HostMetrics> &vhost,
									   const std::shared_ptr<mon::ApplicationMetrics> &app,
									   const std::shared_ptr<mon::StreamMetrics> &stream, const std::vector<std::shared_ptr<mon::StreamMetrics>> &output_streams);

			// GET /v1/vhosts/<vhost_name>/apps/<app_name>/streams/<stream_name>
			ApiResponse OnGetChannel(const std::shared_ptr<http::svr::HttpExchange> &client,
									 const std::shared_ptr<mon::HostMetrics> &vhost,
									 const std::shared_ptr<mon::ApplicationMetrics> &app,
									 const std::shared_ptr<mon::StreamMetrics> &stream, const std::vector<std::shared_ptr<mon::StreamMetrics>> &output_streams);

			// DELETE /v1/vhosts/<vhost_name>/apps/<app_name>/streams/<stream_name>
			ApiResponse OnDeleteChannel(const std::shared_ptr<http::svr::HttpExchange> &client,
										const std::shared_ptr<mon::HostMetrics> &vhost,
										const std::shared_ptr<mon::ApplicationMetrics> &app,
										const std::shared_ptr<mon::StreamMetrics> &stream, const std::vector<std::shared_ptr<mon::StreamMetrics>> &output_streams);
		};
	}  // namespace v1
}  // namespace api
