//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2020 AirenSoft. All rights reserved.
//
//==============================================================================
#include "current_controller.h"

#include "internals/internals_controller.h"
#include "vhosts/vhosts_controller.h"

namespace api
{
	namespace v1
	{
		namespace stats
		{
			void CurrentController::PrepareHandlers()
			{
				RegisterGet(R"()", &CurrentController::OnGetServerMetrics);

				CreateSubController<VHostsController>(R"(\/vhosts)");
				CreateSubController<InternalsController>(R"(\/internals)");
			}

			ApiResponse CurrentController::OnGetServerMetrics(const std::shared_ptr<http::svr::HttpExchange> &client)
			{
				auto serverMetric = MonitorInstance->GetServerMetrics();
				return ::serdes::JsonFromMetrics(serverMetric);
			}
		}  // namespace stats
	}  // namespace v1
}  // namespace api
