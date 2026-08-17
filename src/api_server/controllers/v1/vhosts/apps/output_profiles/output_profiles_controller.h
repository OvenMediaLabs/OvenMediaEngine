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
		class OutputProfilesController : public ControllerBase<OutputProfilesController>
		{
		public:
			void PrepareHandlers() override;

		protected:
			// POST /v1/vhosts/<vhost_name>/apps/<app_name>/outputProfiles
			ApiResponse OnPostOutputProfile(const std::shared_ptr<http::svr::HttpExchange> &client, const Json::Value &request_body,
											const std::shared_ptr<mon::HostMetrics> &vhost,
											const std::shared_ptr<mon::ApplicationMetrics> &app);

			// GET /v1/vhosts/<vhost_name>/apps/<app_name>/outputProfiles
			ApiResponse OnGetOutputProfileList(const std::shared_ptr<http::svr::HttpExchange> &client,
											   const std::shared_ptr<mon::HostMetrics> &vhost,
											   const std::shared_ptr<mon::ApplicationMetrics> &app);

			// GET /v1/vhosts/<vhost_name>/apps/<app_name>/outputProfiles/<output_profile_name>
			ApiResponse OnGetOutputProfile(const std::shared_ptr<http::svr::HttpExchange> &client,
										   const std::shared_ptr<mon::HostMetrics> &vhost,
										   const std::shared_ptr<mon::ApplicationMetrics> &app);

			// PATCH /v1/vhosts/<vhost_name>/apps/<app_name>/outputProfiles/<output_profile_name>
			ApiResponse OnPatchOutputProfile(const std::shared_ptr<http::svr::HttpExchange> &client, const Json::Value &request_body,
											 const std::shared_ptr<mon::HostMetrics> &vhost,
											 const std::shared_ptr<mon::ApplicationMetrics> &app);

			// DELETE /v1/vhosts/<vhost_name>/apps/<app_name>/outputProfiles/<output_profile_name>
			ApiResponse OnDeleteOutputProfile(const std::shared_ptr<http::svr::HttpExchange> &client,
											  const std::shared_ptr<mon::HostMetrics> &vhost,
											  const std::shared_ptr<mon::ApplicationMetrics> &app);
		};
	}  // namespace v1
}  // namespace api
