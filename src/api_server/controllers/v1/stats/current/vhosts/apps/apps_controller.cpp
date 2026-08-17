//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2020 AirenSoft. All rights reserved.
//
//==============================================================================
#include "apps_controller.h"

#include "streams/streams_controller.h"

namespace api
{
	namespace v1
	{
		namespace stats
		{
			void AppsController::PrepareHandlers()
			{
				RegisterGet(R"(\/(?<app_name>[^\/]*))", &AppsController::OnGetApp);

				CreateSubController<StreamsController>(R"(\/(?<app_name>[^\/:]*)\/streams)");
			};

			ApiResponse AppsController::OnGetApp(const std::shared_ptr<http::svr::HttpExchange> &client,
												 const std::shared_ptr<mon::HostMetrics> &vhost,
												 const std::shared_ptr<mon::ApplicationMetrics> &app)
			{
				return ::serdes::JsonFromMetrics(app);
			}
		}  // namespace stats
	}  // namespace v1
}  // namespace api
