//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2020 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include "../../../../controller_base.h"

namespace api
{
	namespace v1
	{
		class StreamsController : public ControllerBase<StreamsController>
		{
		public:
			void PrepareHandlers() override;

		protected:
			// POST /v1/vhosts/<vhost_name>/apps/<app_name>
			ApiResponse OnPostStream(const std::shared_ptr<http::svr::HttpExchange> &client, const Json::Value &request_body,
									 const std::shared_ptr<mon::HostMetrics> &vhost,
									 const std::shared_ptr<mon::ApplicationMetrics> &app);

			// GET /v1/vhosts/<vhost_name>/apps/<app_name>/streams
			ApiResponse OnGetStreamList(const std::shared_ptr<http::svr::HttpExchange> &client,
										const std::shared_ptr<mon::HostMetrics> &vhost,
										const std::shared_ptr<mon::ApplicationMetrics> &app);

			// GET /v1/vhosts/<vhost_name>/apps/<app_name>/streams/<stream_name>
			ApiResponse OnGetStream(const std::shared_ptr<http::svr::HttpExchange> &client,
									const std::shared_ptr<mon::HostMetrics> &vhost,
									const std::shared_ptr<mon::ApplicationMetrics> &app,
									const std::shared_ptr<mon::StreamMetrics> &stream, const std::vector<std::shared_ptr<mon::StreamMetrics>> &output_streams);

			// DELETE /v1/vhosts/<vhost_name>/apps/<app_name>/streams/<stream_name>
			ApiResponse OnDeleteStream(const std::shared_ptr<http::svr::HttpExchange> &client,
									   const std::shared_ptr<mon::HostMetrics> &vhost,
									   const std::shared_ptr<mon::ApplicationMetrics> &app,
									   const std::shared_ptr<mon::StreamMetrics> &stream, const std::vector<std::shared_ptr<mon::StreamMetrics>> &output_streams);
		};
	}  // namespace v1
}  // namespace api
