//=============================================================================
//
//  OvenMediaEngine
//
//  Created by Keukhan
//  Copyright (c) 2025 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <monitoring/monitoring.h>

#include "base/info/overlay.h"

namespace serdes
{
	std::shared_ptr<info::Overlay> OverlayFromJson(const Json::Value &json_body);
	Json::Value JsonFromOverlay(const std::shared_ptr<info::Overlay> &overlay);

	std::shared_ptr<info::OverlayInfo> OverlayInfoFromJson(const Json::Value &json_body);
	Json::Value JsonFromOverlayInfo(const std::shared_ptr<info::OverlayInfo> &overlay_info);
}  // namespace serdes