//==============================================================================
//
//  OvenMediaEngine
//
//  Created by getroot
//  Copyright (c) 2020 AirenSoft. All rights reserved.
//
//==============================================================================
#include "host_metrics.h"

#include "monitoring_private.h"

namespace mon
{
	ov::String HostMetrics::GetInfoString(bool show_children)
	{
		ov::String out_str = ov::String::FormatString("\n[Host Info]\nid(%u), name(%s)\nCreated Time (%s)\n",
													  GetId(), GetName().CStr(),
													  ov::Converter::ToString(_created_time).CStr());

		out_str.Append(CommonMetrics::GetInfoString());

		if (show_children)
		{
			std::shared_lock<std::shared_mutex> lock(_map_guard);
			for (auto const &t : _applications)
			{
				auto &app = t.second;
				out_str.Append(app->GetInfoString());
			}
		}

		return out_str;
	}

	void HostMetrics::ShowInfo(bool show_children)
	{
		logti("%s", GetInfoString(show_children).CStr());
	}

	bool HostMetrics::OnApplicationCreated(const info::Application &app_info)
	{
		std::unique_lock<std::shared_mutex> lock(_map_guard);
		if (_applications.find(app_info.GetId()) != _applications.end())
		{
			return true;
		}

		auto app_metrics = std::make_shared<ApplicationMetrics>(GetSharedPtr(), app_info);
		if (app_metrics == nullptr)
		{
			logte("Cannot create ApplicationMetrics (%s/%s - %s)", GetName().CStr(), app_info.GetVHostAppName().CStr(), app_info.GetUUID().CStr());
			return false;
		}

		_applications[app_info.GetId()] = app_metrics;

		logti("Create ApplicationMetrics(%s/%s) for monitoring", app_info.GetVHostAppName().CStr(), app_info.GetUUID().CStr());
		return true;
	}
	bool HostMetrics::OnApplicationDeleted(const info::Application &app_info)
	{
		std::unique_lock<std::shared_mutex> lock(_map_guard);
		if (_applications.find(app_info.GetId()) == _applications.end())
		{
			return false;
		}
		_applications.erase(app_info.GetId());

		logti("Delete ApplicationMetrics(%s/%s) for monitoring", app_info.GetVHostAppName().CStr(), app_info.GetUUID().CStr());
		return true;
	}

	std::map<uint32_t, std::shared_ptr<ApplicationMetrics>> HostMetrics::GetApplicationMetricsList()
	{
		std::shared_lock<std::shared_mutex> lock(_map_guard);
		return _applications;
	}

	std::shared_ptr<ApplicationMetrics> HostMetrics::GetApplicationMetrics(info::application_id_t application_id)
	{
		std::shared_lock<std::shared_mutex> lock(_map_guard);
		if (_applications.find(application_id) == _applications.end())
		{
			return nullptr;
		}

		return _applications[application_id];
	}
}  // namespace mon