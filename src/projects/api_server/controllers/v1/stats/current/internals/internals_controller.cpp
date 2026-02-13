//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Keukhan Kwon
//  Copyright (c) 2020 AirenSoft. All rights reserved.
//
//==============================================================================
#include "internals_controller.h"

namespace api
{
	namespace v1
	{
		namespace stats
		{
			void InternalsController::PrepareHandlers()
			{
				RegisterGet(R"()", &InternalsController::OnGetInternals);
				RegisterGet(R"(\/queues)", &InternalsController::OnGetQueues);
			};

			ApiResponse InternalsController::OnGetInternals(const std::shared_ptr<http::svr::HttpExchange> &client)
			{
				Json::Value response(Json::ValueType::arrayValue);

				response.append("/v1/stats/current/internals/queues");

				return response;
			}

			ApiResponse InternalsController::OnGetQueues(const std::shared_ptr<http::svr::HttpExchange> &client)
			{
				Json::Value response(Json::ValueType::arrayValue);

				auto serverMetric = MonitorInstance->GetServerMetrics();

				for (auto &[queue_id, metrics] : serverMetric->GetQueueMetricsList())
				{
					Json::Value obj = serdes::JsonFromQueueMetrics(metrics);

					if (obj.isNull())
						continue;

					response.append(obj);
				}

				return response;
			}
		}  // namespace stats
	}  // namespace v1
}  // namespace api