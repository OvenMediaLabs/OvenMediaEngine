//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2020 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include "../../../../../../controller_base.h"

namespace api
{
	namespace v1
	{
		namespace stats
		{
			class StreamsController : public ControllerBase<StreamsController>
			{
			public:
				void PrepareHandlers() override;

			protected:
				ApiResponse OnGetStream(const std::shared_ptr<http::svr::HttpExchange> &client,
										const std::shared_ptr<mon::HostMetrics> &vhost,
										const std::shared_ptr<mon::ApplicationMetrics> &app,
										const std::shared_ptr<mon::StreamMetrics> &stream,
										const std::vector<std::shared_ptr<mon::StreamMetrics>> &output_streams);
			};
		}  // namespace stats
	}  // namespace v1
}  // namespace api
