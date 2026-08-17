//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2021 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <base/ovlibrary/ovlibrary.h>

#include <pugixml-1.9/src/pugixml.hpp>

// Forward declaration
class ReservedStreamMetrics;
namespace mon
{
	class ApplicationMetrics;
	class HostMetrics;
}  // namespace mon

namespace cfg
{
	struct Server;

	namespace serdes
	{
		Json::Value GetMpegTsStreamListFromMetrics(Json::Value providers, const std::map<uint32_t, std::shared_ptr<ReservedStreamMetrics>> &reserved_stream_list);

		MAY_THROWS(cfg::ConfigError)
		Json::Value GetApplicationFromMetrics(const std::shared_ptr<const mon::ApplicationMetrics> &app_metrics);
		MAY_THROWS(cfg::ConfigError)
		Json::Value GetApplicationListFromMetrics(const std::map<uint32_t, std::shared_ptr<mon::ApplicationMetrics>> &app_metrics_list, bool include_dynamic_app);
		MAY_THROWS(cfg::ConfigError)
		Json::Value GetVirtualHostFromMetrics(const std::shared_ptr<mon::HostMetrics> &vhost_metrics, bool include_dynamic_app);
		MAY_THROWS(cfg::ConfigError)
		Json::Value GetVirtualHostListFromMetrics(Json::Value server_config, const std::map<uint32_t, std::shared_ptr<mon::HostMetrics>> &vhost_metrics_list, bool include_dynamic_app);
		MAY_THROWS(cfg::ConfigError)
		Json::Value GetServerJsonFromConfig(const std::shared_ptr<const Server> &server_config, bool include_dynamic_app = false);

		void GetMpegTsStreamListFromMetrics(pugi::xml_node providers_node, const std::map<uint32_t, std::shared_ptr<ReservedStreamMetrics>> &reserved_stream_list);
		MAY_THROWS(cfg::ConfigError)
		void GetApplicationFromMetrics(pugi::xml_node app_node, const std::shared_ptr<const mon::ApplicationMetrics> &app_metrics);
		MAY_THROWS(cfg::ConfigError)
		void GetApplicationListFromMetrics(pugi::xml_node parent_node, const std::map<uint32_t, std::shared_ptr<mon::ApplicationMetrics>> &app_metrics_list, bool include_dynamic_app);
		MAY_THROWS(cfg::ConfigError)
		void GetVirtualHostFromMetrics(pugi::xml_node vhost_node, const std::shared_ptr<mon::HostMetrics> &vhost_metrics, bool include_dynamic_app);
		MAY_THROWS(cfg::ConfigError)
		void GetVirtualHostListFromMetrics(pugi::xml_node parent_node, const std::map<uint32_t, std::shared_ptr<mon::HostMetrics>> &vhost_metrics_list, bool include_dynamic_app);
		MAY_THROWS(cfg::ConfigError)
		pugi::xml_document GetServerXmlFromConfig(const std::shared_ptr<const Server> &server_config, bool include_dynamic_app = false);
	}  // namespace serdes
}  // namespace cfg
