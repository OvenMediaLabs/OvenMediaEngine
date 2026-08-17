//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2020 AirenSoft. All rights reserved.
//
//==============================================================================
#include "application.h"

#include <config/config.h>

#include "common.h"

namespace serdes
{
	Json::Value JsonFromOutputProfile(const cfg::vhost::app::oprf::OutputProfile &output_profile)
	{
		return output_profile.ToJson();
	}

	Json::Value JsonFromApplication(const std::shared_ptr<const mon::ApplicationMetrics> &application)
	{
		Json::Value app = cfg::serdes::GetApplicationFromMetrics(application);

		if (app.isObject())
		{
			app["dynamic"] = application->IsDynamicApp();
		}

		return app;
	}

	void ApplicationFromJson(const Json::Value &json_value, cfg::vhost::app::Application *app_config)
	{
		app_config->SetItemName("Application");
		app_config->FromJson(json_value);
	}

	void OutputProfilesFromJson(const Json::Value &json_value, cfg::vhost::app::oprf::OutputProfiles *output_profile_config)
	{
		output_profile_config->SetItemName("OutputProfiles");
		output_profile_config->FromJson(json_value);
	}
}  // namespace serdes
