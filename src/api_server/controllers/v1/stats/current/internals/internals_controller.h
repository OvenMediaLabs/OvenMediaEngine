//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Keukhan Kwon
//  Copyright (c) 2020 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include "../../../../controller_base.h"

namespace api
{
	namespace v1
	{
		namespace stats
		{
			class InternalsController : public ControllerBase<InternalsController>
			{
			public:
				void PrepareHandlers() override;

			protected:
				ApiResponse OnGetInternals(const std::shared_ptr<http::svr::HttpExchange> &client);
				ApiResponse OnGetQueues(const std::shared_ptr<http::svr::HttpExchange> &client);
			};
		}  // namespace stats
	}  // namespace v1
}  // namespace api
