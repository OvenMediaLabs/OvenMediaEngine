//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2019 AirenSoft. All rights reserved.
//
//==============================================================================

#include "virtual_host.h"
#include "orchestrator_private.h"

namespace ocst
{
	VirtualHost::VirtualHost(const info::Host &new_host_info)
		: _host_info(new_host_info)
	{
		_name = _host_info.GetName();

		for (const auto &host_name : _host_info.GetHost().GetNameList())
		{
			AddHostName(host_name);
		}

		{
			std::lock_guard<std::shared_mutex> lock(_origin_list_mutex);
			// Origin list
			for (const auto &origin_cfg : _host_info.GetOriginList())
			{
				_origin_list.emplace_back(origin_cfg);
			}
		}
		// OriginMapStore
		bool enabled = false;
		auto store = _host_info.GetOriginMapStore(&enabled);
		if (enabled == true)
		{
			// Make ovt base url
			auto ovt_port = cfg::ConfigManager::GetInstance()->GetServer()->GetBind().GetPublishers().GetOvt().GetPort();
			_origin_map_client = std::make_shared<OriginMapClient>(store.GetRedisServer().GetHost(), store.GetRedisServer().GetAuth());
			_is_origin_map_store_enabled = true;

			if (store.GetOriginHostName().IsEmpty() == false)
			{
				_origin_base_url = ov::String::FormatString("ovt://%s:%d", store.GetOriginHostName().CStr(), ovt_port.GetPort());
			}
			else
			{
				logti("OriginMapStore::OriginHostName is not specified. This OriginMapStore can work only as a edge.");
			}
		}
	}

	ov::String VirtualHost::GetName() const
	{
		return _name;
	}

	const info::Host &VirtualHost::GetHostInfo()
	{
		return _host_info;
	}

	ov::String VirtualHost::GetOriginBaseUrl() const
	{
		return _origin_base_url;
	}

	bool VirtualHost::LoadCertificate()
	{
		_host_info.LoadCertificate();
		return true;
	}

	bool VirtualHost::IsOriginMapStoreEnabled() const
	{
		return _is_origin_map_store_enabled;
	}

	std::shared_ptr<OriginMapClient> VirtualHost::GetOriginMapClient() const
	{
		return _origin_map_client;
	}

	std::shared_ptr<Application> VirtualHost::GetApplication(info::application_id_t app_id) const
	{
		std::shared_lock<std::shared_mutex> lock(_app_map_mutex);
		auto iter = _app_map.find(app_id);
		if (iter != _app_map.end())
		{
			return iter->second;
		}

		return nullptr;
	}

	std::shared_ptr<Application> VirtualHost::GetApplication(const info::VHostAppName &vhost_app_name) const
	{
		std::shared_lock<std::shared_mutex> lock(_app_map_mutex);
		for (const auto &item : _app_map)
		{
			if (item.second->GetVHostAppName() == vhost_app_name)
			{
				return item.second;
			}
		}

		return nullptr;
	}

	std::shared_ptr<Application> VirtualHost::GetApplication(const ov::String &app_name) const
	{
		std::shared_lock<std::shared_mutex> lock(_app_map_mutex);
		for (const auto &item : _app_map)
		{
			if (item.second->GetVHostAppName().GetAppName() == app_name)
			{
				return item.second;
			}
		}

		return nullptr;
	}

	std::vector<std::shared_ptr<Application>> VirtualHost::GetApplicationList() const
	{
		std::shared_lock<std::shared_mutex> lock(_app_map_mutex);
		std::vector<std::shared_ptr<Application>> app_list;
		for (const auto &item : _app_map)
		{
			app_list.emplace_back(item.second);
		}

		return app_list;
	}

	bool VirtualHost::CreateApplication(Application::CallbackInterface *callback, const info::Application &app_info)
	{
		auto app = std::make_shared<Application>(callback, app_info);
		if (app == nullptr)
		{
			return false;
		}

		std::lock_guard<std::shared_mutex> lock(_app_map_mutex);
		_app_map.emplace(app_info.GetId(), app);

		return true;
	}

	bool VirtualHost::DeleteApplication(info::application_id_t app_id)
	{
		std::lock_guard<std::shared_mutex> lock(_app_map_mutex);
		auto iter = _app_map.find(app_id);
		if (iter != _app_map.end())
		{
			_app_map.erase(iter);
			return true;
		}

		return false;
	}

	void VirtualHost::SetDynamicApplicationConfig(const cfg::vhost::app::Application &app_cfg_template)
	{
		_app_cfg_template = app_cfg_template;
	}

	const cfg::vhost::app::Application& VirtualHost::GetDynamicApplicationConfigTemplate() const
	{
		return _app_cfg_template;
	}

	bool VirtualHost::FindOriginByRequestedLocation(const ov::String &requested_location, Origin &origin) const
	{
		std::shared_lock<std::shared_mutex> lock(_origin_list_mutex);

		for (const auto &item : _origin_list)
		{
			if (item.IsStrictLocation() == true)
			{
				if (requested_location == item.GetLocation())
				{
					origin = item;
					return true;
				}
			}
			else 
			{
				if (requested_location.HasPrefix(item.GetLocation()))
				{
					origin = item;
					return true;
				}
			}
		}

		return false;
	}

	bool VirtualHost::ValidateDomain(const ov::String &domain) const
	{
		std::shared_lock<std::shared_mutex> lock(_host_names_mutex);
		for (const auto &host_name : _host_names)
		{
			if (host_name.Match(domain) == true)
			{
				return true;
			}
		}

		return false;
	}

	void VirtualHost::AddHostName(const ov::String &host_name)
	{
		std::lock_guard<std::shared_mutex> lock(_host_names_mutex);
		_host_names.emplace_back(host_name);
	}

	VirtualHost::HostName::HostName(const ov::String &host_name)
		: _name(host_name)
	{
		UpdateRegex();
	}

	bool VirtualHost::HostName::UpdateRegex()
	{
		// Escape special characters: '[', '\', '.', '/', '+', '{', '}', '$', '^', '|' to \<char>
		auto special_characters = std::regex(R"([[\\.\/+{}$^|])");
		ov::String escaped = std::regex_replace(_name.CStr(), special_characters, R"(\$&)").c_str();
		// Change '*'/'?' to .<char>
		escaped = escaped.Replace(R"(*)", R"(.*)");
		escaped = escaped.Replace(R"(?)", R"(.?)");
		escaped.Prepend("^");
		escaped.Append("$");

		std::regex expression;

		try
		{
			_regex_for_domain = std::regex(escaped);
		}
		catch (std::exception &e)
		{
			return false;
		}

		return true;
	}
} // namespace ocst