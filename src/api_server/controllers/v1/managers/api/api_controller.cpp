//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2025 AirenSoft. All rights reserved.
//
//==============================================================================
#include "api_controller.h"

#include "api_actions_controller.h"

namespace api::v1::mgrs::api
{
	void ApiController::PrepareHandlers()
	{
		// Branch into action controller
		CreateSubController<ApiActionsController>(R"(:)");
	}
}  // namespace api::v1::mgrs::api
