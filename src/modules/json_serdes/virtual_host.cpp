//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2020 AirenSoft. All rights reserved.
//
//==============================================================================
#include "virtual_host.h"

#include "common.h"

namespace serdes
{
	void VirtualHostFromJson(const Json::Value &json_value, cfg::vhost::VirtualHost *vhost_config)
	{
		vhost_config->SetItemName("VirtualHost");
		vhost_config->SetReadOnly(false);

		vhost_config->FromJson(json_value);
	}

	Json::Value JsonFromVirtualHost(const cfg::vhost::VirtualHost &vhost_config)
	{
		auto vhost_json = vhost_config.ToJson();
		vhost_json.removeMember("applications");

		return vhost_json;
	}
}  // namespace serdes
