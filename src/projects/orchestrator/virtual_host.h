//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2019 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <base/info/host.h>
#include <regex>
#include <modules/origin_map_client/origin_map_client.h>
#include <modules/http/cors/cors_manager.h>

#include "origin.h"
#include "application.h"

namespace ocst
{
	class VirtualHost
	{
	public:
		VirtualHost(const info::Host &new_host_info);

		ov::String GetName() const;
		const info::Host& GetHostInfo();
		bool LoadCertificate();

		void AddHostName(const ov::String &host_name);
		bool FindOriginByRequestedLocation(const ov::String &location, Origin &origin) const;

		void SetDynamicApplicationConfig(const cfg::vhost::app::Application &app_cfg_template);
		const cfg::vhost::app::Application& GetDynamicApplicationConfigTemplate() const;
		
		std::shared_ptr<Application> GetApplication(info::application_id_t app_id) const;
		std::shared_ptr<Application> GetApplication(const info::VHostAppName &vhost_app_name) const;
		std::shared_ptr<Application> GetApplication(const ov::String &app_name) const;

		bool CreateApplication(Application::CallbackInterface *callback, const info::Application &app_info);
		bool DeleteApplication(info::application_id_t app_id);

		std::vector<std::shared_ptr<Application>> GetApplicationList() const;

		// Match the domain name
		bool ValidateDomain(const ov::String &domain) const;

		bool IsOriginMapStoreEnabled() const;
		std::shared_ptr<OriginMapClient> GetOriginMapClient() const;

		ov::String GetOriginBaseUrl() const;
	
	private:
		// Origin Host Info
		info::Host _host_info;

		// The name of VirtualHost (eg: AirenSoft-VHost)
		ov::String _name;

		// Host list
		// IP or Domain Name
		class HostName
		{
		public:
			HostName(const ov::String &host_name);

			// Match the domain name
			bool Match(const ov::String &domain) const
			{
				return std::regex_match(domain.CStr(), _regex_for_domain);
			}

		private:
			bool UpdateRegex();

			// The name of Host in the configuration (eg: *, *.airensoft.com)
			ov::String _name;
			std::regex _regex_for_domain;
		};

		std::vector<HostName> _host_names;
		mutable std::shared_mutex _host_names_mutex;

		// Origin list
		std::vector<Origin> _origin_list;
		mutable std::shared_mutex _origin_list_mutex;

		// OriginMapStore
		bool _is_origin_map_store_enabled = false;
		ov::String _origin_base_url;
		std::shared_ptr<OriginMapClient> _origin_map_client = nullptr;

		// Template of dynamic application configuration
		cfg::vhost::app::Application _app_cfg_template;

		// Application list
		std::map<info::application_id_t, std::shared_ptr<Application>> _app_map;
		mutable std::shared_mutex _app_map_mutex;
	};
}