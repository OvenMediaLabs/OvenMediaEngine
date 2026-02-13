//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2020 AirenSoft. All rights reserved.
//
//==============================================================================
#include "streams_controller.h"

namespace api
{
	namespace v1
	{
		namespace stats
		{
			void StreamsController::PrepareHandlers()
			{
				RegisterGet(R"(\/(?<stream_name>[^\/]*))", &StreamsController::OnGetStream);
			};

			ApiResponse StreamsController::OnGetStream(const std::shared_ptr<http::svr::HttpExchange> &client,
													   const std::shared_ptr<mon::HostMetrics> &vhost,
													   const std::shared_ptr<mon::ApplicationMetrics> &app,
													   const std::shared_ptr<mon::StreamMetrics> &stream,
													   const std::vector<std::shared_ptr<mon::StreamMetrics>> &output_streams)
			{
				return ::serdes::JsonFromMetrics(stream);
			}
		}  // namespace stats
	}  // namespace v1
}  // namespace api
