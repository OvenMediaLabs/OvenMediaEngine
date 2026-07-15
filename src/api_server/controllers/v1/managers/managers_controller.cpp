//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2025 AirenSoft. All rights reserved.
//
//==============================================================================
#include "managers_controller.h"

#include <config/config.h>

#include "../../../api_private.h"
#include "../../../api_server.h"
#include "api/api_controller.h"

namespace api::v1::mgrs
{
	void ManagersController::PrepareHandlers()
	{
		// Branch into `api` controller
		CreateSubController<api::ApiController>(R"(\/api)");
	}
}  // namespace api::v1::mgrs
