//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2020 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include "../../controller_base.h"

namespace api::v1::stats
{
	class StatsController : public ControllerBase<StatsController>
	{
	public:
		void PrepareHandlers() override;
	};
}  // namespace api::v1::stats
