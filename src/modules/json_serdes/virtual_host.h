//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2020 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <config/config.h>

namespace serdes
{
	MAY_THROWS(cfg::ConfigError)
	void VirtualHostFromJson(const Json::Value &json_value, cfg::vhost::VirtualHost *vhost_config);
	MAY_THROWS(cfg::ConfigError)
	Json::Value JsonFromVirtualHost(const cfg::vhost::VirtualHost &vhost_config);
}  // namespace serdes
