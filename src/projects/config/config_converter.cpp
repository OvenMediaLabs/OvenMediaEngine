//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2021 AirenSoft. All rights reserved.
//
//==============================================================================
#include "config_converter.h"

#include <monitoring/monitoring.h>

#include "config_private.h"

namespace cfg
{
	namespace serdes
	{
		// #region ========== JSON ==========
		Json::Value GetMpegTsStreamListFromMetrics(Json::Value providers, const std::map<uint32_t, std::shared_ptr<ReservedStreamMetrics>> &reserved_stream_list)
		{
			auto &mpeg_ts_provider = providers["mpegts"];

			// Reset stream list (Currently, streams contains the settings that were loaded at the beginning of the OME run)
			auto &streams		   = mpeg_ts_provider["streams"];
			streams				   = Json::arrayValue;

			if (reserved_stream_list.size() == 0)
			{
				// Nothing to do
				return Json::nullValue;
			}

			for (auto &key : reserved_stream_list)
			{
				Json::Value stream;

				auto &reserved_stream = key.second;
				auto &uri			  = reserved_stream->GetStreamUri();

				auto name			  = reserved_stream->GetReservedStreamName();
				stream["name"]		  = name.CStr();

				auto port			  = ov::String::FormatString("%d/%s", uri.Port(), uri.Scheme().LowerCaseString().CStr());
				stream["port"]		  = port.CStr();

				streams.append(stream);
			}

			return providers;
		}

		Json::Value GetApplicationFromMetrics(const std::shared_ptr<const mon::ApplicationMetrics> &app_metrics)
		{
			auto app				  = app_metrics->GetConfig().ToJson();

			auto reserved_stream_list = app_metrics->GetReservedStreamMetricsMap();

			if (reserved_stream_list.size() > 0)
			{
				app["providers"] = GetMpegTsStreamListFromMetrics(app["providers"], reserved_stream_list);
			}

			return app;
		}

		Json::Value GetApplicationListFromMetrics(const std::map<uint32_t, std::shared_ptr<mon::ApplicationMetrics>> &app_metrics_list, bool include_dynamic_app)
		{
			Json::Value apps = Json::arrayValue;

			for (auto &app_pair : app_metrics_list)
			{
				auto &app_metrics = app_pair.second;

				if ((include_dynamic_app == false) && app_metrics->IsDynamicApp())
				{
					// Don't save the dynamic app
					continue;
				}

				apps.append(GetApplicationFromMetrics(app_metrics));
			}

			return apps;
		}

		Json::Value GetVirtualHostFromMetrics(const std::shared_ptr<mon::HostMetrics> &vhost_metrics, bool include_dynamic_app)
		{
			Json::Value vhost	  = std::static_pointer_cast<cfg::vhost::VirtualHost>(vhost_metrics)->ToJson();

			// Overwrite application list (Currently, vhost contains the settings that were loaded at the beginning of the OME run)
			vhost["applications"] = GetApplicationListFromMetrics(vhost_metrics->GetApplicationMetricsList(), include_dynamic_app);

			return vhost;
		}

		Json::Value GetVirtualHostListFromMetrics(Json::Value server_config, const std::map<uint32_t, std::shared_ptr<mon::HostMetrics>> &vhost_metrics_list, bool include_dynamic_app)
		{
			if (vhost_metrics_list.size() > 0)
			{
				// Save the VirtualHost list

				// Reset virtual host list (Currently, server_config contains the settings that were loaded at the beginning of the OME run)
				Json::Value &vhosts = server_config["virtualHosts"];
				vhosts				= Json::arrayValue;

				for (auto vhost_metrics_pair : vhost_metrics_list)
				{
					vhosts.append(GetVirtualHostFromMetrics(vhost_metrics_pair.second, include_dynamic_app));
				}
			}
			else
			{
				if (server_config.isMember("virtualHosts") == false)
				{
					logte("The values of ConfigManager and Monitoring do not match even though the VirtualHost list is immutable. (VirtualHost is not loaded on Monitoring?)");
					OV_ASSERT2(false);

					return Json::nullValue;
				}

				// Nothing to do
			}

			return server_config;
		}

		Json::Value GetServerJsonFromConfig(const std::shared_ptr<const cfg::Server> &server_config, bool include_dynamic_app)
		{
			// Convert server config to JSON to obtain <Bind> and etc
			return cfg::serdes::GetVirtualHostListFromMetrics(server_config->ToJson(), mon::Monitoring::GetInstance()->GetHostMetricsList(), include_dynamic_app);
		}
		// #endregion JSON

		// #region ========== XML ==========
		void GetMpegTsStreamListFromMetrics(pugi::xml_node providers_node, const std::map<uint32_t, std::shared_ptr<ReservedStreamMetrics>> &reserved_stream_list)
		{
			auto mpeg_ts_provider_node = providers_node.child("MPEGTS");

			// Reset stream list (Currently, streams contains the settings that were loaded at the beginning of the OME run)
			mpeg_ts_provider_node.remove_child("StreamMap");

			if (reserved_stream_list.size() == 0)
			{
				// Nothing to do
				return;
			}

			auto streamMapNode = mpeg_ts_provider_node.append_child("StreamMap");

			for (auto &key : reserved_stream_list)
			{
				auto &reserved_stream = key.second;
				auto &uri			  = reserved_stream->GetStreamUri();

				auto stream_node	  = streamMapNode.append_child("Stream");

				auto port			  = ov::String::FormatString("%d/%s", uri.Port(), uri.Scheme().LowerCaseString().CStr());

				stream_node.append_child("Name").text().set(reserved_stream->GetReservedStreamName());
				stream_node.append_child("Port").text().set(port);
			}
		}

		void GetApplicationFromMetrics(pugi::xml_node app_node, const std::shared_ptr<const mon::ApplicationMetrics> &app_metrics)
		{
			app_metrics->GetConfig().ToXml(app_node);

			auto reserved_stream_list = app_metrics->GetReservedStreamMetricsMap();

			if (reserved_stream_list.size() > 0)
			{
				auto providers_node = app_node.child("Providers");

				if (providers_node.empty())
				{
					providers_node = app_node.append_child("Providers");
				}

				GetMpegTsStreamListFromMetrics(providers_node, reserved_stream_list);
			}
		}

		void GetApplicationListFromMetrics(pugi::xml_node parent_node, const std::map<uint32_t, std::shared_ptr<mon::ApplicationMetrics>> &app_metrics_list, bool include_dynamic_app)
		{
			// Remove <Applications> node before insertion
			parent_node.remove_child("Applications");

			if (app_metrics_list.empty())
			{
				// Empty application list
				return;
			}

			auto app_list_node = parent_node.append_child("Applications");

			for (auto &app_pair : app_metrics_list)
			{
				auto &app_metrics = app_pair.second;

				if ((include_dynamic_app == false) && app_metrics->IsDynamicApp())
				{
					// Don't save the dynamic app
					continue;
				}

				auto app_node = app_list_node.append_child(pugi::node_element);
				GetApplicationFromMetrics(app_node, app_metrics);
			}
		}

		void GetVirtualHostFromMetrics(pugi::xml_node vhost_node, const std::shared_ptr<mon::HostMetrics> &vhost_metrics, bool include_dynamic_app)
		{
			std::static_pointer_cast<cfg::vhost::VirtualHost>(vhost_metrics)->ToXml(vhost_node);

			// Overwrite application list (Currently, vhost contains the settings that were loaded at the beginning of the OME run)
			GetApplicationListFromMetrics(vhost_node, vhost_metrics->GetApplicationMetricsList(), include_dynamic_app);
		}

		void GetVirtualHostListFromMetrics(pugi::xml_node parent_node, const std::map<uint32_t, std::shared_ptr<mon::HostMetrics>> &vhost_metrics_list, bool include_dynamic_app)
		{
			parent_node.remove_child("VirtualHosts");

			if (vhost_metrics_list.empty())
			{
				// Empty vhost list
				return;
			}

			auto vhost_list_node = parent_node.append_child("VirtualHosts");

			// Save the VirtualHost list

			for (auto vhost_metrics_pair : vhost_metrics_list)
			{
				auto vhost_node = vhost_list_node.append_child(pugi::node_element);

				GetVirtualHostFromMetrics(vhost_node, vhost_metrics_pair.second, include_dynamic_app);
			}
		}

		pugi::xml_document GetServerXmlFromConfig(const std::shared_ptr<const Server> &server_config, bool include_dynamic_app)
		{
			// Convert server config to XML to obtain <Bind> and etc
			auto doc		 = server_config->ToXml();

			auto server_node = doc.child("Server");

			if (server_node.empty() == false)
			{
				GetVirtualHostListFromMetrics(server_node, mon::Monitoring::GetInstance()->GetHostMetricsList(), include_dynamic_app);
			}
			else
			{
				OV_ASSERT2(false);
			}

			return doc;
		}
		// #endregion XML

	}  // namespace serdes
}  // namespace cfg
