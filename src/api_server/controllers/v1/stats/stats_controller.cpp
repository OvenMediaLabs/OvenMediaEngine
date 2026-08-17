//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2020 AirenSoft. All rights reserved.
//
//==============================================================================
#include "stats_controller.h"

#include "current/current_controller.h"

namespace api::v1::stats
{
	void StatsController::PrepareHandlers()
	{
		CreateSubController<CurrentController>(R"(\/current)");
	};
}  // namespace api::v1::stats
