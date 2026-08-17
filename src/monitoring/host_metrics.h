//==============================================================================
//
//  OvenMediaEngine
//
//  Created by getroot
//  Copyright (c) 2020 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <shared_mutex>

#include "application_metrics.h"
#include "base/common_types.h"
#include "base/info/host.h"
#include "common_metrics.h"

namespace mon
{
	class HostMetrics : public info::Host, public CommonMetrics, public ov::EnableSharedFromThis<HostMetrics>
	{
	public:
		HostMetrics(const info::Host &host_info)
			: info::Host(host_info)
		{
		}

		~HostMetrics()
		{
			_applications.clear();
		}

		void Release()
		{
			std::map<uint32_t, std::shared_ptr<ApplicationMetrics>> applications;
			{
				std::unique_lock<std::shared_mutex> lock(_map_guard);
				applications = std::move(_applications);
			}

			for (const auto &app : applications)
			{
				app.second->Release();
			}
		}

		ov::String GetInfoString(bool show_children = true) override;
		void ShowInfo(bool show_children = true) override;

		bool OnApplicationCreated(const info::Application &app_info);
		bool OnApplicationDeleted(const info::Application &app_info);

		std::map<uint32_t, std::shared_ptr<ApplicationMetrics>> GetApplicationMetricsList();

		std::shared_ptr<ApplicationMetrics> GetApplicationMetrics(info::application_id_t application_id);
		std::shared_ptr<ApplicationMetrics> GetApplicationMetrics(const info::Application &app_info)
		{
			return GetApplicationMetrics(app_info.GetId());
		}

	private:
		std::shared_mutex _map_guard;
		std::map<uint32_t, std::shared_ptr<ApplicationMetrics>> _applications;
	};
}  // namespace mon