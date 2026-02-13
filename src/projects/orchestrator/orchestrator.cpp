//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2019 AirenSoft. All rights reserved.
//
//==============================================================================
#include "orchestrator.h"

#include <base/mediarouter/mediarouter_interface.h>
#include <base/provider/pull_provider/stream_props.h>
#include <base/provider/stream.h>
#include <mediarouter/mediarouter.h>
#include <monitoring/monitoring.h>

#include <functional>

#include "orchestrator_private.h"

namespace ocst
{
	bool Orchestrator::StartServer(const std::shared_ptr<const cfg::Server> &server_config)
	{
		_server_config = server_config;

		mon::Monitoring::GetInstance()->OnServerStarted(server_config);

		auto &vhost_conf_list = _server_config->GetVirtualHostList();

		if (CreateVirtualHosts(vhost_conf_list) == false)
		{
			return false;
		}

		auto dynamic_app_removal = server_config->GetModules().GetDynamicAppRemoval();
		if (dynamic_app_removal.IsEnabled() == true)
		{
			// TODO(Getroot): 2024-10-07 // It may have critical bug. It should be fixed.
			_timer.Push(
				[this](void *paramter) -> ov::DelayQueueAction {
					DeleteUnusedDynamicApplications();
					return ov::DelayQueueAction::Repeat;
				},
				10000);
			_timer.Start();
		}

		return true;
	}

	void Orchestrator::DeleteUnusedDynamicApplications()
	{
		// [Job] Delete dynamic application if there are no streams
		{
			// Delete dynamic application if there are no streams
			auto vhost_list = GetVirtualHostList();
			for (auto &vhost_item : vhost_list)
			{
				auto app_list = vhost_item->GetApplicationList();
				for (auto &app : app_list)
				{
					auto &app_info = app->GetAppInfo();

					// Delete dynamic application if there are no streams for 60 seconds
					if (app_info.IsDynamicApp() == true && app->IsUnusedFor(60) == true)
					{
						logti("There are no streams in the dynamic application for 60 seconds. Delete the application: %s", app_info.GetVHostAppName().CStr());
						auto result = DeleteApplication(app_info);
						if (result != Result::Succeeded)
						{
							logte("Could not delete dynamic application: %s", app_info.GetVHostAppName().CStr());
							continue;
						}
					}
				}
			}
		}
	}

	ocst::Result Orchestrator::Release()
	{
		auto vhost_list = GetVirtualHostList();
		for (auto &vhost_item : vhost_list)
		{
			mon::Monitoring::GetInstance()->OnHostDeleted(vhost_item->GetHostInfo());

			auto app_list = vhost_item->GetApplicationList();
			for (auto &app : app_list)
			{
				auto &app_info = app->GetAppInfo();

				auto result = DeleteApplication(app_info);
				if (result != Result::Succeeded)
				{
					logte("Could not delete application: %s", app_info.GetVHostAppName().CStr());
					continue;
				}
			}
		}

		{
			std::lock_guard<std::shared_mutex> lock(_virtual_host_mutex);
			_virtual_host_list.clear();
			_virtual_host_map.clear();
		}
		mon::Monitoring::GetInstance()->Release();

		return Result::Succeeded;
	}

	bool Orchestrator::CreateVirtualHosts(const std::vector<cfg::vhost::VirtualHost> &vhost_conf_list)
	{
		for (const auto &vhost_conf : vhost_conf_list)
		{
			// Create VirtualHost in Orchestrator
			if (CreateVirtualHost(vhost_conf) != Result::Succeeded)
			{
				logte("Could not create VirtualHost(%s)", vhost_conf.GetName().CStr());
				return false;
			}
		}

		return true;
	}

	Result Orchestrator::CreateVirtualHost(const cfg::vhost::VirtualHost &vhost_cfg)
	{
		info::Host vhost_info(_server_config->GetName(), _server_config->GetID(), vhost_cfg);

		auto result = CreateVirtualHost(vhost_info);
		switch (result)
		{
			case ocst::Result::Failed:
				logtc("Failed to create a virtual host: %s", vhost_cfg.GetName().CStr());
				return result;

			case ocst::Result::Succeeded:
				break;

			case ocst::Result::Exists:
				logtc("Duplicate virtual host [%s] found. Please check the settings.", vhost_cfg.GetName().CStr());
				return result;

			case ocst::Result::NotExists:
				// This should never happen
				OV_ASSERT2(false);
				logtc("Internal error occurred (THIS IS A BUG)");
				return result;
		}

		// Create Applications
		for (const auto &app_cfg : vhost_info.GetApplicationList())
		{
			if (app_cfg.GetName() == "*")
			{
				// wildcard application is template for dynamic applications
				if (CreateApplicationTemplate(vhost_info, app_cfg) != Result::Succeeded)
				{
					return Result::Failed;
				}
			}
			else
			{
				if (CreateApplication(vhost_info, app_cfg) != Result::Succeeded)
				{
					// Rollback
					DeleteVirtualHost(vhost_info);
					return Result::Failed;
				}
			}
		}

		return Result::Succeeded;
	}

	std::optional<info::Host> Orchestrator::GetHostInfo(ov::String vhost_name)
	{
		auto vhost = GetVirtualHost(vhost_name);
		if (vhost != nullptr)
		{
			return vhost->GetHostInfo();
		}

		return std::nullopt;
	}

	ocst::Result Orchestrator::CreateApplication(const info::Host &host_info, const cfg::vhost::app::Application &app_config, bool is_dynamic)
	{
		auto vhost_name = host_info.GetName();

		info::Application app_info(host_info, GetNextAppId(), ResolveApplicationName(vhost_name, app_config.GetName()), app_config, is_dynamic);
		auto result = CreateApplication(vhost_name, app_info);
		switch (result)
		{
			case ocst::Result::Failed:
				logtc("Failed to create an application: %s/%s", vhost_name.CStr(), app_config.GetName().CStr());
				break;

			case ocst::Result::Succeeded:
				break;

			case ocst::Result::Exists:
				logtc("Duplicate application [%s/%s] found. Please check the settings.", vhost_name.CStr(), app_config.GetName().CStr());
				break;

			case ocst::Result::NotExists:
				// This should never happen
				OV_ASSERT2(false);
				logtc("Internal error occurred (THIS IS A BUG)");
				break;
		}

		return result;
	}

	ocst::Result Orchestrator::DeleteApplication(const info::Application &app_info)
	{
		auto &vhost_app_name = app_info.GetVHostAppName();
		if (vhost_app_name.IsValid() == false)
		{
			return Result::Failed;
		}

		auto result = DeleteApplication(vhost_app_name.GetVHostName(), app_info.GetId());
		switch (result)
		{
			case ocst::Result::Failed:
				logtc("Failed to delete an application: %s", app_info.GetVHostAppName().CStr());
				return result;

			case ocst::Result::Succeeded:
				break;

			case ocst::Result::Exists:
				// This should never happen
				OV_ASSERT2(false);
				logtc("Duplicate application [%s] found. Please check the settings.", app_info.GetVHostAppName().CStr());
				return result;

			case ocst::Result::NotExists:
				logtc("Unable to delete application (does not exist): %s", app_info.GetVHostAppName().CStr());
				return result;
		}

		return result;
	}

	ocst::Result Orchestrator::DeleteApplication(const ov::String &vhost_name, info::application_id_t app_id)
	{
		auto vhost = GetVirtualHost(vhost_name);
		if (vhost == nullptr)
		{
			return Result::NotExists;
		}

		auto app = vhost->GetApplication(app_id);
		if (app == nullptr)
		{
			logti("Application %d does not exists", app_id);
			return Result::NotExists;
		}

		auto &app_info = app->GetAppInfo();

		logti("Trying to delete an application: [%s] (%u)", app_info.GetVHostAppName().CStr(), app_info.GetId());

		mon::Monitoring::GetInstance()->OnApplicationDeleted(app_info);

		if (vhost->DeleteApplication(app_id) == false)
		{
			logte("Could not delete an application: [%s]", app_info.GetVHostAppName().CStr());
			return Result::Failed;
		}

		if (_media_router != nullptr)
		{
			_media_router->UnregisterObserverApp(app_info, app->GetSharedPtrAs<MediaRouterApplicationObserver>());
		}

		{
			logtt("Notifying modules for the delete event...");
			auto module_list = GetModuleList();
			// Notify modules of deletion events
			for (auto module = module_list.rbegin(); module != module_list.rend(); ++module)
			{
				auto module_interface = module->GetModuleInterface();

				logtt("Notifying %p (%s) for the delete event (%s)", module_interface.get(), GetModuleTypeName(module_interface->GetModuleType()).CStr(), app_info.GetVHostAppName().CStr());

				if (module_interface->OnDeleteApplication(app_info) == false)
				{
					logte("The module %p (%s) returns error while deleting the application [%s]",
						module_interface.get(), GetModuleTypeName(module_interface->GetModuleType()).CStr(), app_info.GetVHostAppName().CStr());

					// Ignore this error - some providers may not have generated the app
				}
				else
				{
					logtt("The module %p (%s) returns true", module_interface.get(), GetModuleTypeName(module_interface->GetModuleType()).CStr());
				}
			}
		}

		return Result::Succeeded;
	}

	std::vector<std::shared_ptr<ocst::VirtualHost>> Orchestrator::GetVirtualHostList() const
	{
		std::shared_lock<std::shared_mutex> lock(_virtual_host_mutex);
		return _virtual_host_list;
	}

	std::vector<Module> Orchestrator::GetModuleList() const
	{
		std::shared_lock<std::shared_mutex> lock(_module_list_mutex);
		return _module_list;
	}

	bool Orchestrator::RegisterModule(const std::shared_ptr<ModuleInterface> &module_interface)
	{
		if (module_interface == nullptr)
		{
			return false;
		}

		auto type = module_interface->GetModuleType();

		{
			std::shared_lock<std::shared_mutex> lock(_module_list_mutex);
			// Check if module exists
			for (auto &module : _module_list)
			{
				if (module.GetModuleInterface() == module_interface)
				{
					if (module.GetType() == type)
					{
						logtw("%s module (%p) is already registered", GetModuleTypeName(type).CStr(), module_interface.get());
					}
					else
					{
						logtw("The module type was %s (%p), but now %s", GetModuleTypeName(module.GetType()).CStr(), module_interface.get(), GetModuleTypeName(type).CStr());
					}

					OV_ASSERT2(false);
					return false;
				}
			}
		}

		{
			std::lock_guard<std::shared_mutex> lock(_module_list_mutex);
			_module_list.emplace_back(type, module_interface);
		}

		if (module_interface->GetModuleType() == ModuleType::MediaRouter)
		{
			auto media_router = std::dynamic_pointer_cast<MediaRouter>(module_interface);

			OV_ASSERT2(media_router != nullptr);

			_media_router = media_router;
		}

		logtt("%s module (%p) is registered", GetModuleTypeName(type).CStr(), module_interface.get());

		return true;
	}

	bool Orchestrator::UnregisterModule(const std::shared_ptr<ModuleInterface> &module_interface)
	{
		if (module_interface == nullptr)
		{
			OV_ASSERT2(module_interface != nullptr);
			return false;
		}

		{
			std::lock_guard<std::shared_mutex> lock(_module_list_mutex);
			for (auto info = _module_list.begin(); info != _module_list.end(); ++info)
			{
				if (info->GetModuleInterface() == module_interface)
				{
					_module_list.erase(info);
					logtt("%s module (%p) is unregistered", GetModuleTypeName(info->GetType()).CStr(), module_interface.get());
					return true;
				}
			}
		}

		logtw("%s module (%p) not found", GetModuleTypeName(module_interface->GetModuleType()).CStr(), module_interface.get());
		OV_ASSERT2(false);

		return false;
	}

	ov::String Orchestrator::GetVhostNameFromDomain(const ov::String &domain_name) const
	{
		if (domain_name.IsEmpty() == false)
		{
			auto vhost_list = GetVirtualHostList();
			for (auto &vhost : vhost_list)
			{
				if (vhost->ValidateDomain(domain_name) == true)
				{
					return vhost->GetName();
				}
			}
		}

		return "";
	}

	info::VHostAppName Orchestrator::ResolveApplicationNameFromDomain(const ov::String &domain_name, const ov::String &app_name) const
	{
		auto vhost_name = GetVhostNameFromDomain(domain_name);

		if (vhost_name.IsEmpty())
		{
			logtw("Could not find VirtualHost for domain: %s", domain_name.CStr());
		}

		auto resolved = ResolveApplicationName(vhost_name, app_name);

		logtt("Resolved application name: %s (from domain: %s, app: %s)", resolved.CStr(), domain_name.CStr(), app_name.CStr());

		return resolved;
	}

	bool Orchestrator::RequestPullStreamWithUrls(
		const std::shared_ptr<const ov::Url> &request_from,
		const info::VHostAppName &vhost_app_name, const ov::String &stream_name,
		const std::vector<ov::String> &url_list, off_t offset, const std::shared_ptr<pvd::PullStreamProperties> &properties)
	{
		if (url_list.empty() == true)
		{
			logtw("RequestPullStream must have at least one URL");
			return false;
		}

		auto url = url_list[0];
		auto parsed_url = ov::Url::Parse(url);

		if (parsed_url == nullptr)
		{
			// Invalid URL
			logte("Pull stream is requested for invalid URL: %s", url.CStr());
			return false;
		}
		
		auto app_info = info::Application::GetInvalidApplication();
		// Check if the application does exists
		app_info = GetApplicationInfo(vhost_app_name);
		if (app_info.IsValid() == false)
		{
			// Create a new application using application template if exists

			// Get vhost info
			auto vhost = GetVirtualHost(vhost_app_name.GetVHostName());
			if (vhost == nullptr)
			{
				logte("Could not find VirtualHost for the stream: [%s/%s]", vhost_app_name.CStr(), stream_name.CStr());
				return false;
			}

			// Copy application template configuration
			auto app_cfg = vhost->GetDynamicApplicationConfigTemplate();
			if (app_cfg.IsParsed() == false)
			{
				logte("Could not find application template for the stream: [%s/%s]", vhost_app_name.CStr(), stream_name.CStr());
				return false;
			}

			// Set the application name
			app_cfg.SetName(vhost_app_name.GetAppName());

			logti("Trying to create dynamic application for the stream: [%s/%s]", vhost_app_name.CStr(), stream_name.CStr());
			if (CreateApplication(vhost->GetHostInfo(), app_cfg, true) != Result::Succeeded)
			{
				logte("Could not create application for the stream: [%s/%s]", vhost_app_name.CStr(), stream_name.CStr());
				return false;
			}

			app_info = GetApplicationInfo(vhost_app_name);
			if (app_info.IsValid() == false)
			{
				// MUST NOT HAPPEN
				logte("Could not find created application for the stream: [%s/%s]", vhost_app_name.CStr(), stream_name.CStr());
				return false;
			}
		}

		auto provider_module = GetProviderModuleForScheme(parsed_url->Scheme());
		if (provider_module == nullptr)
		{
			logte("Could not find provider for the stream: [%s/%s]", vhost_app_name.CStr(), stream_name.CStr());
			return false;
		}
		
		logti("Trying to pull stream [%s/%s] from provider using URL: %s",
				vhost_app_name.CStr(), stream_name.CStr(),
				GetModuleTypeName(provider_module->GetModuleType()).CStr());

		auto stream = provider_module->PullStream(request_from, app_info, stream_name, url_list, offset, properties);
		if (stream != nullptr)
		{
			logti("The stream was pulled successfully: [%s/%s] (%u)",
					vhost_app_name.CStr(), stream_name.CStr(), stream->GetId());

			return true;
		}

		logte("Could not pull stream [%s/%s] from provider: %s",
				vhost_app_name.CStr(), stream_name.CStr(),
				GetModuleTypeName(provider_module->GetModuleType()).CStr());

		return true;
	}

	// Pull a stream using Origin map
	bool Orchestrator::RequestPullStreamWithOriginMap(
		const std::shared_ptr<const ov::Url> &request_from,
		const info::VHostAppName &vhost_app_name, const ov::String &stream_name,
		off_t offset)
	{
		std::shared_ptr<PullProviderModuleInterface> provider_module;
		auto app_info = info::Application::GetInvalidApplication();
		std::vector<ov::String> url_list;
		Origin matched_origin;
		auto &host_name = request_from->Host();
		
		std::vector<ov::String> url_list_in_map;
		if (GetUrlListForLocation(vhost_app_name, host_name, stream_name, matched_origin, url_list_in_map) == false)
		{
			logte("Could not find Origin for the stream: [%s/%s]", vhost_app_name.CStr(), stream_name.CStr());
			return false;
		}

		if (matched_origin.IsValid() == false)
		{
			// Origin/Domain can never be nullptr if origin is found
			OV_ASSERT2(matched_origin.IsValid() == true);
			logte("Could not find URL list for the stream: [%s/%s]", vhost_app_name.CStr(), stream_name.CStr());
			return false;
		}

		provider_module = GetProviderModuleForScheme(matched_origin.GetScheme());
		if (provider_module == nullptr)
		{
			logte("Could not find provider for the stream: [%s/%s]", vhost_app_name.CStr(), stream_name.CStr());
			return false;
		}

		// Check if the application does exists
		app_info = GetApplicationInfo(vhost_app_name);
		if (app_info.IsValid() == false)
		{
			// Create a new application using application template if exists

			// Get vhost info
			auto vhost = GetVirtualHost(vhost_app_name.GetVHostName());

			// Copy application template configuration
			auto app_cfg = vhost->GetDynamicApplicationConfigTemplate();
			if (app_cfg.IsParsed() == false)
			{
				logte("Could not find application template for the stream: [%s/%s]", vhost_app_name.CStr(), stream_name.CStr());
				return false;
			}

			app_cfg.SetName(vhost_app_name.GetAppName());

			logti("Trying to create dynamic application for the stream: [%s/%s]", vhost_app_name.CStr(), stream_name.CStr());
			if (CreateApplication(vhost->GetHostInfo(), app_cfg, true) != Result::Succeeded)
			{
				logte("Could not create application for the stream: [%s/%s]", vhost_app_name.CStr(), stream_name.CStr());
				return false;
			}

			app_info = GetApplicationInfo(vhost_app_name);
			if (app_info.IsValid() == false)
			{
				// MUST NOT HAPPEN
				logte("Could not find created application for the stream: [%s/%s]", vhost_app_name.CStr(), stream_name.CStr());
				return false;
			}
		}

		if (matched_origin.IsForwardQueryParamsEnabled() && request_from->HasQueryString())
		{
			// Combine query string with the URL
			for (auto url : url_list_in_map)
			{
				auto parsed_url = ov::Url::Parse(url);
				if (parsed_url == nullptr)
				{
					logte("Invalid URL: %s", url.CStr());
					continue;
				}

				url.Append(parsed_url->HasQueryString() ? '&' : '?');
				url.Append(request_from->Query());

				url_list.push_back(url);
			}
		}
		else
		{
			url_list = std::move(url_list_in_map);
		}

		logti("Trying to pull stream [%s/%s] from provider using origin map: %s",
			  vhost_app_name.CStr(), stream_name.CStr(),
			  GetModuleTypeName(provider_module->GetModuleType()).CStr());

		// Use Matched Origin information as an properties in Pull Stream.
		auto properties = std::make_shared<pvd::PullStreamProperties>();
		properties->EnablePersistent(matched_origin.IsPersistent());
		properties->EnableFailback(matched_origin.IsFailback());
		properties->EnableRelay(matched_origin.IsRelay());
		properties->EnableIgnoreRtcpSRTimestamp(matched_origin.IsIgnoreRtcpSrTimestamp());
		properties->EnableFromOriginMapStore(false);

		auto stream = provider_module->PullStream(request_from, app_info, stream_name, url_list, offset, properties);
		if (stream != nullptr)
		{
			return true;
		}

		logte("Could not pull stream [%s/%s] from provider: %s",
			  vhost_app_name.CStr(), stream_name.CStr(),
			  GetModuleTypeName(provider_module->GetModuleType()).CStr());

		return false;
	}

	/// Delete PullStream
	CommonErrorCode Orchestrator::TerminateStream(const info::VHostAppName &vhost_app_name, const ov::String &stream_name)
	{
		auto stream = GetProviderStream(vhost_app_name, stream_name);
		if (stream == nullptr)
		{
			return CommonErrorCode::NOT_FOUND;
		}

		if (stream->Terminate() == false)
		{
			return CommonErrorCode::ERROR;
		}

		return CommonErrorCode::SUCCESS;
	}

	bool Orchestrator::OnStreamCreated(const info::Application &app_info, const std::shared_ptr<info::Stream> &info)
	{
		logtt("%s/%s stream of %s is created", app_info.GetVHostAppName().CStr(), info->GetName().CStr(), info->IsInputStream() ? "inbound" : "outbound");

		return true;
	}

	bool Orchestrator::OnStreamDeleted(const info::Application &app_info, const std::shared_ptr<info::Stream> &info)
	{
		logti("%s/%s stream of %s is deleted", app_info.GetVHostAppName().CStr(), info->GetName().CStr(), info->IsInputStream() ? "inbound" : "outbound");

		return true;
	}

	bool Orchestrator::OnStreamPrepared(const info::Application &app_info, const std::shared_ptr<info::Stream> &info)
	{
		logtt("%s/%s stream of %s is parsed", app_info.GetVHostAppName().CStr(), info->GetName().CStr(), info->IsInputStream() ? "inbound" : "outbound");

		return true;
	}

	bool Orchestrator::OnStreamUpdated(const info::Application &app_info, const std::shared_ptr<info::Stream> &info)
	{
		logtt("%s/%s stream of %s is updated", app_info.GetVHostAppName().CStr(), info->GetName().CStr(), info->IsInputStream() ? "inbound" : "outbound");
		return true;
	}

	std::shared_ptr<pvd::Provider> Orchestrator::GetProviderFromType(const ProviderType type)
	{
		auto module_list = GetModuleList();
		for (auto &module : module_list)
		{
			if (module.GetType() == ModuleType::PushProvider || module.GetType() == ModuleType::PullProvider)
			{
				auto provider = module.GetModuleAs<pvd::Provider>();
				if (provider == nullptr)
				{
					OV_ASSERT(provider != nullptr, "Provider must inherit from pvd::Provider");
					continue;
				}

				if (provider->GetProviderType() == type)
				{
					return provider;
				}
			}
		}

		return nullptr;
	}

	std::shared_ptr<pub::Publisher> Orchestrator::GetPublisherFromType(const PublisherType type)
	{
		auto module_list = GetModuleList();
		for (auto &module : module_list)
		{
			if (module.GetType() == ModuleType::Publisher)
			{
				auto publisher = module.GetModuleAs<pub::Publisher>();
				if (publisher == nullptr)
				{
					OV_ASSERT(publisher != nullptr, "Provider must inherit from pub::Publisher");
					continue;
				}

				if (publisher->GetPublisherType() == type)
				{
					return publisher;
				}
			}
		}

		return nullptr;
	}

	std::shared_ptr<pvd::Stream> Orchestrator::GetProviderStream(const std::shared_ptr<const info::Stream> &stream_info)
	{
		// Get ProviderType from SourceType
		ProviderType provider_type = stream_info->GetProviderType();
		if (provider_type == ProviderType::Unknown)
		{
			return nullptr;
		}

		auto provider = std::dynamic_pointer_cast<pvd::Provider>(GetProviderFromType(provider_type));
		if (provider == nullptr)
		{
			return nullptr;
		}

		auto application = provider->GetApplicationByName(stream_info->GetApplicationInfo().GetVHostAppName());
		if (application == nullptr)
		{
			return nullptr;
		}

		auto prov_stream = application->GetStreamByName(stream_info->GetName());
		if (prov_stream == nullptr)
		{
			return nullptr;
		}
		
		return prov_stream;
	}

	CommonErrorCode Orchestrator::IsExistStreamInOriginMapStore(const info::VHostAppName &vhost_app_name, const ov::String &stream_name) const
	{
		auto vhost = GetVirtualHost(vhost_app_name);
		if (vhost == nullptr)
		{
			// Error
			return CommonErrorCode::ERROR;
		}

		if (vhost->IsOriginMapStoreEnabled() == false)
		{
			// disabled by user
			return CommonErrorCode::DISABLED;
		}

		auto client = vhost->GetOriginMapClient();
		if (client == nullptr)
		{
			// Error
			return CommonErrorCode::ERROR;
		}

		auto app_stream_name = ov::String::FormatString("%s/%s", vhost_app_name.GetAppName().CStr(), stream_name.CStr());

		ov::String temp_str;
		return client->GetOrigin(app_stream_name, temp_str);
	}

	std::shared_ptr<ov::Url> Orchestrator::GetOriginUrlFromOriginMapStore(const info::VHostAppName &vhost_app_name, const ov::String &stream_name) const
	{
		auto vhost = GetVirtualHost(vhost_app_name);
		if (vhost == nullptr)
		{
			// Error
			return nullptr;
		}

		if (vhost->IsOriginMapStoreEnabled() == false)
		{
			// disabled by user
			return nullptr;
		}

		auto client = vhost->GetOriginMapClient();
		if (client == nullptr)
		{
			// Error
			return nullptr;
		}

		auto app_stream_name = ov::String::FormatString("%s/%s", vhost_app_name.GetAppName().CStr(), stream_name.CStr());

		ov::String url_str;
		if (client->GetOrigin(app_stream_name, url_str) == CommonErrorCode::SUCCESS)
		{
			return ov::Url::Parse(url_str);
		}

		return nullptr;
	}

	CommonErrorCode Orchestrator::RegisterStreamToOriginMapStore(const info::VHostAppName &vhost_app_name, const ov::String &stream_name)
	{
		auto vhost = GetVirtualHost(vhost_app_name);
		if (vhost == nullptr)
		{
			// Error
			return CommonErrorCode::ERROR;
		}

		if (vhost->IsOriginMapStoreEnabled() == false)
		{
			// disabled by user
			return CommonErrorCode::DISABLED;
		}

		auto client = vhost->GetOriginMapClient();
		if (client == nullptr)
		{
			// Error
			return CommonErrorCode::ERROR;
		}

		auto app_stream_name = ov::String::FormatString("%s/%s", vhost_app_name.GetAppName().CStr(), stream_name.CStr());
		auto ovt_url = ov::String::FormatString("%s/%s", vhost->GetOriginBaseUrl().CStr(), app_stream_name.CStr());
		if (client->RequestRegister(app_stream_name, ovt_url) == true)
		{
			return CommonErrorCode::SUCCESS;
		}

		return CommonErrorCode::ERROR;
	}

	CommonErrorCode Orchestrator::UnregisterStreamFromOriginMapStore(const info::VHostAppName &vhost_app_name, const ov::String &stream_name)
	{
		auto vhost = GetVirtualHost(vhost_app_name);
		if (vhost == nullptr)
		{
			// Error
			return CommonErrorCode::ERROR;
		}

		if (vhost->IsOriginMapStoreEnabled() == false)
		{
			// disabled by user
			return CommonErrorCode::DISABLED;
		}

		auto client = vhost->GetOriginMapClient();
		if (client == nullptr)
		{
			// Error
			return CommonErrorCode::ERROR;
		}

		auto app_stream_name = ov::String::FormatString("%s/%s", vhost_app_name.GetAppName().CStr(), stream_name.CStr());

		if (client->RequestUnregister(app_stream_name) == true)
		{
			return CommonErrorCode::SUCCESS;
		}

		return CommonErrorCode::ERROR;
	}

	bool Orchestrator::CheckIfStreamExist(const info::VHostAppName &vhost_app_name, const ov::String &stream_name)
	{
		auto stream = GetProviderStream(vhost_app_name, stream_name);
		if (stream == nullptr)
		{
			// Error
			return false;
		}

		return true;
	}

	// Mirror Stream
	CommonErrorCode Orchestrator::MirrorStream(std::shared_ptr<MediaRouterStreamTap> &stream_tap, const info::VHostAppName &vhost_app_name, const ov::String &stream_name, MediaRouterInterface::MirrorPosition posision)
	{
		if (_media_router == nullptr)
		{
			return CommonErrorCode::INVALID_STATE;
		}

		return _media_router->MirrorStream(stream_tap, vhost_app_name, stream_name, posision);
	}

	CommonErrorCode Orchestrator::UnmirrorStream(const std::shared_ptr<MediaRouterStreamTap> &stream_tap)
	{
		if (_media_router == nullptr)
		{
			return CommonErrorCode::INVALID_STATE;
		}

		return _media_router->UnmirrorStream(stream_tap);
	}


	////////////////////////////////////////////////////
	// Internal Functions
	////////////////////////////////////////////////////

	info::application_id_t Orchestrator::GetNextAppId()
	{
		return _last_application_id++;
	}

	std::shared_ptr<pvd::Provider> Orchestrator::GetProviderForScheme(const ov::String &scheme)
	{
		auto lower_scheme = scheme.LowerCaseString();

		logtt("Obtaining ProviderType for scheme %s...", scheme.CStr());

		ProviderType type;

		if (lower_scheme == "rtmp")
		{
			type = ProviderType::Rtmp;
		}
		else if (lower_scheme == "rtsp")
		{
			type = ProviderType::RtspPull;
		}
		else if (lower_scheme == "rtspc")
		{
			type = ProviderType::RtspPull;
		}
		else if (lower_scheme == "ovt")
		{
			type = ProviderType::Ovt;
		}
		else if (lower_scheme == "file")
		{
			type = ProviderType::File;
		}
		else
		{
			logte("Could not find a provider for scheme [%s]", scheme.CStr());
			return nullptr;
		}

		// Find the provider
		{
			std::shared_lock<std::shared_mutex> guard(_module_list_mutex);
			for (const auto &module : _module_list)
			{
				if (module.GetType() == ModuleType::PullProvider)
				{
					auto provider_module = module.GetModuleAs<pvd::Provider>();
					if (provider_module == nullptr)
					{
						OV_ASSERT(provider_module != nullptr, "Provider must inherit from pvd::Provider");
						continue;
					}

					if (provider_module->GetProviderType() == type)
					{
						return provider_module;
					}
				}
			}
		}

		logtw("Provider (%d) is not found for scheme %s", ov::ToUnderlyingType(type), scheme.CStr());
		return nullptr;
	}

	std::shared_ptr<PullProviderModuleInterface> Orchestrator::GetProviderModuleForScheme(const ov::String &scheme)
	{
		auto provider = GetProviderForScheme(scheme);
		auto provider_module = std::dynamic_pointer_cast<PullProviderModuleInterface>(provider);

		OV_ASSERT((provider == nullptr) || (provider_module != nullptr),
				  "Provider (%d) must inherit from ProviderModuleInterface",
				  ov::ToUnderlyingType(provider->GetProviderType()));

		return provider_module;
	}

	std::shared_ptr<pvd::Provider> Orchestrator::GetProviderForUrl(const ov::String &url)
	{
		// Find a provider type using the scheme
		auto parsed_url = ov::Url::Parse(url);

		if (parsed_url == nullptr)
		{
			logtw("Could not parse URL: %s", url.CStr());
			return nullptr;
		}

		logtt("Obtaining ProviderType for URL %s...", url.CStr());

		return GetProviderForScheme(parsed_url->Scheme());
	}

	info::VHostAppName Orchestrator::ResolveApplicationName(const ov::String &vhost_name, const ov::String &app_name) const
	{
		// Replace all # to _
		return info::VHostAppName(vhost_name, app_name);
	}

	Result Orchestrator::CreateVirtualHost(const info::Host &vhost_info)
	{
		if(GetVirtualHost(vhost_info.GetName()) != nullptr)
		{
			// Duplicated VirtualHostName
			return Result::Exists;
		}

		auto vhost = std::make_shared<VirtualHost>(vhost_info);

		{
			std::lock_guard<std::shared_mutex> guard(_virtual_host_mutex);
			_virtual_host_map[vhost_info.GetName()] = vhost;
			_virtual_host_list.push_back(vhost);
		}

		// Notification 
		{
			auto module_list = GetModuleList();
			for (auto &module : module_list)
			{
				auto module_interface = module.GetModuleInterface();

				logtt("Notifying %p (%s) for the create event (%s)", module_interface.get(), GetModuleTypeName(module_interface->GetModuleType()).CStr(), vhost_info.GetName().CStr());

				if (module_interface->OnCreateHost(vhost_info))
				{
					logtt("The module %p (%s) returns true", module_interface.get(), GetModuleTypeName(module_interface->GetModuleType()).CStr());
				}
				else
				{
					logte("The module %p (%s) returns error while creating the vhost [%s]",
						module_interface.get(), GetModuleTypeName(module_interface->GetModuleType()).CStr(), vhost_info.GetName().CStr());
				}
			}
		}

		mon::Monitoring::GetInstance()->OnHostCreated(vhost_info);

		return Result::Succeeded;
	}

	Result Orchestrator::DeleteVirtualHost(const info::Host &vhost_info)
	{
		bool found = false;
		{
			std::lock_guard<std::shared_mutex> guard(_virtual_host_mutex);
			auto it = _virtual_host_list.begin();
			while(it != _virtual_host_list.end())
			{
				auto vhost_item = *it;
				if(vhost_item->GetName() == vhost_info.GetName())
				{
					_virtual_host_list.erase(it);
					_virtual_host_map.erase(vhost_item->GetName());
					found = true;
					break;
				}

				it++;
			}
		}

		if (found)
		{
			// Notification
			{
				auto module_list = GetModuleList();
				for (auto &module : module_list)
				{
					auto module_interface = module.GetModuleInterface();

					logtt("Notifying %p (%s) for the create event (%s)", module_interface.get(), GetModuleTypeName(module_interface->GetModuleType()).CStr(), vhost_info.GetName().CStr());

					if (module_interface->OnDeleteHost(vhost_info))
					{
						logtt("The module %p (%s) returns true", module_interface.get(), GetModuleTypeName(module_interface->GetModuleType()).CStr());
					}
					else
					{
						logte("The module %p (%s) returns error while deleting the vhost [%s]",
							module_interface.get(), GetModuleTypeName(module_interface->GetModuleType()).CStr(), vhost_info.GetName().CStr());
					}
				}
			}
			
			mon::Monitoring::GetInstance()->OnHostDeleted(vhost_info);
			return Result::Succeeded;
		}

		return Result::NotExists;
	}

	CommonErrorCode Orchestrator::ReloadAllCertificates()
	{
		Result total_result = Result::Succeeded;

		auto vhost_list = GetVirtualHostList();
		for (auto &vhost : vhost_list)
		{
			auto result = ReloadCertificate(vhost);
			if (result != Result::Succeeded)
			{
				total_result = Result::Failed;
			}
		}

		if (total_result == Result::Succeeded)
		{
			logti("All certificates are reloaded successfully");
			return CommonErrorCode::SUCCESS;
		}
		else
		{
			logte("Some certificates are failed to reload");
		}

		return CommonErrorCode::ERROR;
	}

	
	CommonErrorCode Orchestrator::ReloadCertificate(const ov::String &vhost_name)
	{
		auto vhost = GetVirtualHost(vhost_name);
		if (vhost == nullptr)
		{
			return CommonErrorCode::NOT_FOUND;
		}

		auto result = ReloadCertificate(vhost);
		switch (result)
		{
			case ocst::Result::Failed:
				logte("Failed to reload a certificate of virtual host: %s", vhost_name.CStr());
				return CommonErrorCode::ERROR;

			case ocst::Result::Succeeded:
				break;

			case ocst::Result::Exists:
				// This should never happen
				OV_ASSERT2(false);
				logtc("Failed to reload a certificate of virtual host(Conflict): %s", vhost_name.CStr());
				return CommonErrorCode::ERROR;

			case ocst::Result::NotExists:
				logte("Could not find vhost : %s", vhost_name.CStr());
				return CommonErrorCode::NOT_FOUND;
		}

		return CommonErrorCode::SUCCESS;
	}

	ocst::Result Orchestrator::ReloadCertificate(const std::shared_ptr<VirtualHost> &vhost)
	{
		auto &vhost_info = vhost->GetHostInfo();

		if (vhost->LoadCertificate() == false)
		{
			logte("Could not load certificate for vhost [%s]", vhost_info.GetName().CStr());
			return Result::Failed;
		}

		{
			// Notification
			auto module_list = GetModuleList();
			for (auto &module : module_list)
			{
				auto module_interface = module.GetModuleInterface();

				logtt("Notifying %p (%s) for the create event (%s)", module_interface.get(), GetModuleTypeName(module_interface->GetModuleType()).CStr(), vhost_info.GetName().CStr());

				if (module_interface->OnUpdateCertificate(vhost_info))
				{
					logtt("The module %p (%s) returns true", module_interface.get(), GetModuleTypeName(module_interface->GetModuleType()).CStr());
				}
				else
				{
					logte("The module %p (%s) returns error while updating certificate of the vhost [%s]",
						module_interface.get(), GetModuleTypeName(module_interface->GetModuleType()).CStr(), vhost_info.GetName().CStr());
				}
			}
		}

		return Result::Succeeded;
	}

	std::shared_ptr<ocst::VirtualHost> Orchestrator::GetVirtualHost(const ov::String &vhost_name)
	{
		std::shared_lock<std::shared_mutex> guard(_virtual_host_mutex);
		auto vhost_item = _virtual_host_map.find(vhost_name);
		if (vhost_item == _virtual_host_map.end())
		{
			return nullptr;
		}

		return vhost_item->second;
	}

	std::shared_ptr<const ocst::VirtualHost> Orchestrator::GetVirtualHost(const ov::String &vhost_name) const
	{
		std::shared_lock<std::shared_mutex> guard(_virtual_host_mutex);
		auto vhost_item = _virtual_host_map.find(vhost_name);
		if (vhost_item == _virtual_host_map.end())
		{
			return nullptr;
		}

		return vhost_item->second;
	}

	std::shared_ptr<ocst::VirtualHost> Orchestrator::GetVirtualHost(const info::VHostAppName &vhost_app_name)
	{
		if (vhost_app_name.IsValid())
		{
			return GetVirtualHost(vhost_app_name.GetVHostName());
		}

		// vhost_app_name must be valid
		OV_ASSERT2(false);
		return nullptr;
	}

	std::shared_ptr<const ocst::VirtualHost> Orchestrator::GetVirtualHost(const info::VHostAppName &vhost_app_name) const
	{
		if (vhost_app_name.IsValid())
		{
			return GetVirtualHost(vhost_app_name.GetVHostName());
		}

		// vhost_app_name must be valid
		OV_ASSERT2(false);
		return nullptr;
	}

	bool Orchestrator::GetUrlListForLocation(const info::VHostAppName &vhost_app_name, const ov::String &host_name, const ov::String &stream_name, Origin &matched_origin, std::vector<ov::String> &url_list)
	{
		auto vhost = GetVirtualHost(vhost_app_name);
		if (vhost == nullptr)
		{
			logte("Could not find VirtualHost for the stream: [%s/%s]", vhost_app_name.CStr(), stream_name.CStr());
			return false;
		}

		// Find the origin using the location
		ov::String requested_location = ov::String::FormatString("/%s/%s", vhost_app_name.GetAppName().CStr(), stream_name.CStr());
		logtt("Trying to find the item from origin_list that match location: %s", requested_location.CStr());
		Origin found_matched_origin;
		if (vhost->FindOriginByRequestedLocation(requested_location, found_matched_origin) == false)
		{
			logti("Could not find the origin for the location: %s", requested_location.CStr());
			return false;
		}

		if (found_matched_origin.MakeUrlListForRequestedLocation(requested_location, url_list) == false)
		{
			logte("Could not make URL list for the location: %s", requested_location.CStr());
			return false;
		}

		matched_origin = found_matched_origin;

		return true;
	}

	Result Orchestrator::CreateApplicationTemplate(const info::Host &host_info, const cfg::vhost::app::Application &app_config)
	{
		if (app_config.GetName() != "*")
		{
			logtw("Application template name must be \"*\" : %s", app_config.GetName().CStr());
			return Result::Failed;
		}

		auto vhost = GetVirtualHost(host_info.GetName());
		if (vhost == nullptr)
		{
			logtw("Host not found for vhost: %s", host_info.GetName().CStr());
			return Result::Failed;
		}

		vhost->SetDynamicApplicationConfig(app_config);

		return Result::Succeeded;
	}

	ocst::Result Orchestrator::CreateApplication(const ov::String &vhost_name, const info::Application &app_info)
	{
		auto vhost = GetVirtualHost(vhost_name);
		if (vhost == nullptr)
		{
			logtw("Host not found for vhost: %s", vhost_name.CStr());
			return Result::Failed;
		}

		if (vhost->GetApplication(app_info.GetVHostAppName()) != nullptr)
		{
			logtw("Application %s already exists", app_info.GetVHostAppName().CStr());
			return Result::Exists;
		}

		logti("Trying to create an application: [%s]", app_info.GetVHostAppName().CStr());

		if (vhost->CreateApplication(this, app_info) == false)
		{
			logte("Could not create an application: [%s]", app_info.GetVHostAppName().CStr());
			return Result::Failed;
		}

		mon::Monitoring::GetInstance()->OnApplicationCreated(app_info);

		// Notify modules of creation events
		bool succeeded = true;
		{
			auto module_list = GetModuleList();
			for (auto &module : module_list)
			{
				auto module_interface = module.GetModuleInterface();

				logtt("Notifying %p (%s) for the create event (%s)", module_interface.get(), GetModuleTypeName(module_interface->GetModuleType()).CStr(), app_info.GetVHostAppName().CStr());

				if (module_interface->OnCreateApplication(app_info))
				{
					logtt("The module %p (%s) returns true", module_interface.get(), GetModuleTypeName(module_interface->GetModuleType()).CStr());
				}
				else
				{
					logte("The module %p (%s) returns error while creating the application [%s]",
						module_interface.get(), GetModuleTypeName(module_interface->GetModuleType()).CStr(), app_info.GetVHostAppName().CStr());
					succeeded = false;
					break;
				}
			}
		}

		// TODO: Need to be refactored
		// Since Orchestrator registers itself as MediaRouter observer last, OnStreamCreated and OnStreamDeleted events are received last.
		// Orchestrator::OnStreamDeleted and application deletion can proceed simultaneously if the orchestrator does not receive the event at the 
		// very end, so if stream deletion is still in progress in another module, this may cause a conflict.
		// Therefore, we need a guaranteed way Orchestrator to receive the event last, 
		// not the way the orchestrator registers itself with the MediaRouter last and receives the event last. 
		// (Now, it's working because MediaRouter registers an observer using push_back to the vector.)
		if (_media_router != nullptr)
		{
			auto new_app = vhost->GetApplication(app_info.GetId());
			if (new_app != nullptr)
			{
				_media_router->RegisterObserverApp(app_info, new_app->GetSharedPtrAs<MediaRouterApplicationObserver>());
			}
			else
			{
				logte("Could not find the application [%s] after creating it", app_info.GetVHostAppName().CStr());
				succeeded = false;
			}
		}

		if (succeeded)
		{
			return Result::Succeeded;
		}

		logte("Trying to rollback for the application [%s]", app_info.GetVHostAppName().CStr());
		return DeleteApplication(app_info);
	}

	std::shared_ptr<Application> Orchestrator::GetApplication(const info::VHostAppName &vhost_app_name) const
	{
		if (vhost_app_name.IsValid())
		{
			auto &vhost_name = vhost_app_name.GetVHostName();
			auto vhost = GetVirtualHost(vhost_name);
			if (vhost != nullptr)
			{
				return vhost->GetApplication(vhost_app_name);
			}
		}

		return nullptr;
	}

	const info::Application &Orchestrator::GetApplicationInfo(const info::VHostAppName &vhost_app_name) const
	{
		auto app = GetApplication(vhost_app_name);
		if (app == nullptr)
		{
			return info::Application::GetInvalidApplication();
		}

		return app->GetAppInfo();
	}

	const info::Application &Orchestrator::GetApplicationInfo(const ov::String &vhost_name, const ov::String &app_name) const
	{
		return GetApplicationInfo(ResolveApplicationName(vhost_name, app_name));
	}

	const info::Application &Orchestrator::GetApplicationInfo(const ov::String &vhost_name, info::application_id_t app_id) const
	{
		auto vhost = GetVirtualHost(vhost_name);
		if (vhost != nullptr)
		{
			auto app = vhost->GetApplication(app_id);
			if (app != nullptr)
			{
				return app->GetAppInfo();
			}
		}

		return info::Application::GetInvalidApplication();
	}

	std::shared_ptr<pvd::Stream> Orchestrator::GetProviderStream(const info::VHostAppName &vhost_app_name, const ov::String &stream_name)
	{
		auto app = GetApplication(vhost_app_name);

		if (app != nullptr)
		{
			return app->GetProviderStream(stream_name);
		}

		return nullptr;
	}

	std::shared_ptr<pub::Stream> Orchestrator::GetPublisherStream(PublisherType publisher_type, const std::shared_ptr<const info::Stream> &stream_info)
	{
		auto publisher = GetPublisherFromType(publisher_type);
		if (publisher == nullptr)
		{
			return nullptr;
		}

		auto application = publisher->GetApplicationByName(stream_info->GetApplicationInfo().GetVHostAppName());
		if (application == nullptr)
		{
			return nullptr;
		}

		auto pub_stream = application->GetStream(stream_info->GetName());
		if (pub_stream == nullptr)
		{
			return nullptr;
		}

		return pub_stream;
	}

}  // namespace ocst
