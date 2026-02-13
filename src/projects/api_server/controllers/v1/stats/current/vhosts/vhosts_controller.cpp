//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2020 AirenSoft. All rights reserved.
//
//==============================================================================
#include "vhosts_controller.h"

#include "apps/apps_controller.h"

namespace api
{
	namespace v1
	{
		namespace stats
		{
			void VHostsController::PrepareHandlers()
			{
				RegisterGet(R"(\/(?<vhost_name>[^\/]*))", &VHostsController::OnGetVhost);

				CreateSubController<AppsController>(R"(\/(?<vhost_name>[^\/]*)\/apps)");
			};

			ApiResponse VHostsController::OnGetVhost(const std::shared_ptr<http::svr::HttpExchange> &client,
													 const std::shared_ptr<mon::HostMetrics> &vhost)
			{
				return ::serdes::JsonFromMetrics(vhost);
			}
		}  // namespace stats
	}  // namespace v1
}  // namespace api
