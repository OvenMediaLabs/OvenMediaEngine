//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2025 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include "../../../controller_base.h"

namespace api::v1::mgrs::api
{
	class ApiController : public ControllerBase<ApiController>
	{
	public:
		void PrepareHandlers() override;
	};
}  // namespace api::v1::mgrs::api
