//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2020 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include "../../controller_base.h"

namespace api
{
	namespace v1
	{
		class VHostsController : public ControllerBase<VHostsController>
		{
		public:
			void PrepareHandlers() override;

			// POST /v1/vhosts
			ApiResponse OnPostVHost(const std::shared_ptr<http::svr::HttpExchange> &client, const Json::Value &request_body);

			// GET /v1/vhosts
			ApiResponse OnGetVHostList(const std::shared_ptr<http::svr::HttpExchange> &client);

			// GET /v1/vhosts/<vhost_name>
			ApiResponse OnGetVHost(const std::shared_ptr<http::svr::HttpExchange> &client,
								   const std::shared_ptr<mon::HostMetrics> &vhost);

			// DELETE /v1/vhosts/<vhost_name>
			ApiResponse OnDeleteVHost(const std::shared_ptr<http::svr::HttpExchange> &client,
									  const std::shared_ptr<mon::HostMetrics> &vhost);
		};
	}  // namespace v1
}  // namespace api
