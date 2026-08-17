//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2020 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <monitoring/monitoring.h>
#include "base/info/push.h"

namespace serdes
{
	std::shared_ptr<info::Push>  PushFromJson(const Json::Value &json_body);
	Json::Value JsonFromPush(const std::shared_ptr<info::Push> &record);
}  // namespace serdes