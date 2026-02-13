//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Keukhan
//  Copyright (c) 2025 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <monitoring/monitoring.h>
#include "base/info/event_forward.h"
namespace serdes
{
	std::shared_ptr<info::EventForward> EventForwardFromJson(const Json::Value &json_body);
	Json::Value JsonFromEventForward(const std::shared_ptr<info::EventForward> &event_forward);
}  // namespace serdes