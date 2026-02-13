//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2020 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <modules/http/server/http_server.h>
#include <monitoring/monitoring.h>

namespace serdes
{
	Json::Value JsonFromOutputProfile(const cfg::vhost::app::oprf::OutputProfile &output_profile);

	MAY_THROWS(cfg::ConfigError)
	Json::Value JsonFromApplication(const std::shared_ptr<const mon::ApplicationMetrics> &application);

	MAY_THROWS(cfg::ConfigError)
	void ApplicationFromJson(const Json::Value &json_value, cfg::vhost::app::Application *app_config);

	MAY_THROWS(cfg::ConfigError)
	void OutputProfilesFromJson(const Json::Value &json_value, cfg::vhost::app::oprf::OutputProfiles *output_profile_config);
}  // namespace serdes
