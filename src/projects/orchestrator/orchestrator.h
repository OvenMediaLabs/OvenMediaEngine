//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2019 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once
#include <base/info/host.h>
#include <base/mediarouter/mediarouter_interface.h>
#include <base/provider/provider.h>
#include <base/publisher/publisher.h>

#include "virtual_host.h"
#include "module.h"

namespace ocst
{
	class Orchestrator : public ov::Singleton<Orchestrator>, 
							public Application::CallbackInterface
	{
	public:
		/// Register the module
		///
		/// @param module Module to register
		///
		/// @return If the module is registered or passed a different type from the previously registered type, false is returned.
		/// Otherwise, true is returned.
		bool RegisterModule(const std::shared_ptr<ModuleInterface> &module);

		/// Unregister the module
		///
		/// @param module Module to unregister
		///
		/// @return If the module is not already registered, false is returned. Otherwise, true is returned.
		bool UnregisterModule(const std::shared_ptr<ModuleInterface> &module);

		// Create VirtualHost in the settings and instruct application creation to all registered modules.
		// So, StartServer should be called after registering all modules with RegisterModule.
		bool StartServer(const std::shared_ptr<const cfg::Server> &server_config);
		Result Release();

		Result CreateVirtualHost(const cfg::vhost::VirtualHost &vhost_cfg);
		Result CreateVirtualHost(const info::Host &vhost_info);
		Result ReloadCertificate(const std::shared_ptr<VirtualHost> &vhost);

		Result DeleteVirtualHost(const info::Host &vhost_info);
		CommonErrorCode ReloadCertificate(const ov::String &vhost_name);
		CommonErrorCode ReloadAllCertificates();

		std::optional<info::Host> GetHostInfo(ov::String vhost_name);

		bool CreateVirtualHosts(const std::vector<cfg::vhost::VirtualHost> &vhost_conf_list);

		/// Create an application and notify the modules
		///
		/// @param vhost_name A name of VirtualHost
		/// @param app_config Application configuration to create
		///
		/// @return Creation result
		///
		/// @note Automatically DeleteApplication() when application creation fails
		Result CreateApplication(const info::Host &vhost_info, const cfg::vhost::app::Application &app_config, bool is_dynamic = false);
		Result CreateApplication(const ov::String &vhost_name, const info::Application &app_info);
		/// Delete the application and notify the modules
		///
		/// @param app_info Application information to delete
		///
		/// @return
		///
		/// @note If an error occurs during deletion, do not recreate the application
		Result DeleteApplication(const info::Application &app_info);
		Result DeleteApplication(const ov::String &vhost_name, info::application_id_t app_id);

		ov::String GetVhostNameFromDomain(const ov::String &domain_name) const;

		/// Generate an application name for vhost/app
		///
		/// @param vhost_name A name of VirtualHost
		/// @param app_name An application name
		///
		/// @return A new application name corresponding to vhost/app
		info::VHostAppName ResolveApplicationName(const ov::String &vhost_name, const ov::String &app_name) const;

		///  Generate an application name for domain/app
		///
		/// @param domain_name A name of the domain
		/// @param app_name An application name
		///
		/// @return A new application name corresponding to domain/app
		info::VHostAppName ResolveApplicationNameFromDomain(const ov::String &domain_name, const ov::String &app_name) const;

		const info::Application &GetApplicationInfo(const ov::String &vhost_name, const ov::String &app_name) const;
		const info::Application &GetApplicationInfo(const info::VHostAppName &vhost_app_name) const;

		bool RequestPullStreamWithUrls(
			const std::shared_ptr<const ov::Url> &request_from,
			const info::VHostAppName &vhost_app_name, const ov::String &stream_name,
			const std::vector<ov::String> &url_list, off_t offset,
			const std::shared_ptr<pvd::PullStreamProperties> &properties = nullptr);

		/// Pull a stream using specified URL with offset
		///
		/// @param request_from Source from which RequestPullStream() invoked (Mainly provided when requested by Publisher)
		/// @param vhost_app_name When the URL is pulled, its stream is created in this vhost_name and app_name
		/// @param stream_name When the URL is pulled, its stream is created in this stream_name
		/// @param url URL to pull
		/// @param offset Parameters to be used when you want to pull from a certain point (available only when the provider supports that)
		bool RequestPullStreamWithUrl(
			const std::shared_ptr<const ov::Url> &request_from,
			const info::VHostAppName &vhost_app_name, const ov::String &stream_name,
			const ov::String &url, off_t offset)
		{
			return RequestPullStreamWithUrls(request_from, vhost_app_name, stream_name, {url}, offset);
		}

		/// Pull a stream using specified URL
		///
		/// @param request_from Source from which RequestPullStream() invoked (Mainly provided when requested by Publisher)
		/// @param vhost_app_name When the URL is pulled, its stream is created in this vhost_name and app_name
		/// @param stream_name When the URL is pulled, its stream is created in this stream_name
		/// @param url URL to pull
		bool RequestPullStreamWithUrl(
			const std::shared_ptr<const ov::Url> &request_from,
			const info::VHostAppName &vhost_app_name, const ov::String &stream_name,
			const ov::String &url)
		{
			return RequestPullStreamWithUrl(request_from, vhost_app_name, stream_name, url, 0);
		}

		/// Pull a stream using Origin map with offset
		///
		/// @param request_from Source from which RequestPullStream() invoked (Mainly provided when requested by Publisher)
		/// @param vhost_app_name When the URL is pulled, its stream is created in this vhost_name and app_name
		/// @param stream_name When the URL is pulled, its stream is created in this stream_name
		/// @param offset Parameters to be used when you want to pull from a certain point (available only when the provider supports that)
		bool RequestPullStreamWithOriginMap(
			const std::shared_ptr<const ov::Url> &request_from,
			const info::VHostAppName &vhost_app_name, const ov::String &stream_name,
			off_t offset);

		/// Pull a stream using Origin map with offset
		///
		/// @param request_from Source from which RequestPullStream() invoked (Mainly provided when requested by Publisher)
		/// @param vhost_app_name When the URL is pulled, its stream is created in this vhost_name and app_name
		/// @param stream_name When the URL is pulled, its stream is created in this stream_name
		bool RequestPullStreamWithOriginMap(
			const std::shared_ptr<const ov::Url> &request_from,
			const info::VHostAppName &vhost_app_name, const ov::String &stream_name)
		{
			return RequestPullStreamWithOriginMap(request_from, vhost_app_name, stream_name, 0);
		}
		
		/// Release Pulled Stream
		CommonErrorCode TerminateStream(const info::VHostAppName &vhost_app_name, const ov::String &stream_name);

		/// Find Provider from ProviderType
		std::shared_ptr<pvd::Provider> GetProviderFromType(const ProviderType type);
		/// Find Publisher from PublisherType
		std::shared_ptr<pub::Publisher> GetPublisherFromType(const PublisherType type);

		/// Find Provider Stream from StreamInfo
		std::shared_ptr<pvd::Stream> GetProviderStream(const std::shared_ptr<const info::Stream> &stream_info);
		/// Find Publisher Stream from StreamInfo
		std::shared_ptr<pub::Stream> GetPublisherStream(PublisherType publisher_type, const std::shared_ptr<const info::Stream> &stream_info);

		// OriginMapStore
		// key : <app/stream>
		// value : ovt://host:port/<app/stream>
		CommonErrorCode IsExistStreamInOriginMapStore(const info::VHostAppName &vhost_app_name, const ov::String &stream_name) const;
		std::shared_ptr<ov::Url> GetOriginUrlFromOriginMapStore(const info::VHostAppName &vhost_app_name, const ov::String &stream_name) const;
		CommonErrorCode RegisterStreamToOriginMapStore(const info::VHostAppName &vhost_app_name, const ov::String &stream_name);
		CommonErrorCode UnregisterStreamFromOriginMapStore(const info::VHostAppName &vhost_app_name, const ov::String &stream_name);

		// Mirror Stream
		bool CheckIfStreamExist(const info::VHostAppName &vhost_app_name, const ov::String &stream_name);
		CommonErrorCode MirrorStream(std::shared_ptr<MediaRouterStreamTap> &stream_tap, const info::VHostAppName &vhost_app_name, const ov::String &stream_name, MediaRouterInterface::MirrorPosition posision);
		CommonErrorCode UnmirrorStream(const std::shared_ptr<MediaRouterStreamTap> &stream_tap);

		//--------------------------------------------------------------------
		// Implementation of ocst::Application::CallbackInterface
		//--------------------------------------------------------------------
		bool OnStreamCreated(const info::Application &app_info, const std::shared_ptr<info::Stream> &info) override;
		bool OnStreamDeleted(const info::Application &app_info, const std::shared_ptr<info::Stream> &info) override;
		bool OnStreamPrepared(const info::Application &app_info, const std::shared_ptr<info::Stream> &info) override;
		bool OnStreamUpdated(const info::Application &app_info, const std::shared_ptr<info::Stream> &info) override;

	private:
		void DeleteUnusedDynamicApplications();

		info::application_id_t GetNextAppId();

		std::shared_ptr<pvd::Provider> GetProviderForScheme(const ov::String &scheme);
		std::shared_ptr<PullProviderModuleInterface> GetProviderModuleForScheme(const ov::String &scheme);
		std::shared_ptr<pvd::Provider> GetProviderForUrl(const ov::String &url);

		std::shared_ptr<VirtualHost> GetVirtualHost(const ov::String &vhost_name);
		std::shared_ptr<const VirtualHost> GetVirtualHost(const ov::String &vhost_name) const;
		std::shared_ptr<VirtualHost> GetVirtualHost(const info::VHostAppName &vhost_app_name);
		std::shared_ptr<const VirtualHost> GetVirtualHost(const info::VHostAppName &vhost_app_name) const;

		Result CreateApplicationTemplate(const info::Host &host_info, const cfg::vhost::app::Application &app_config);

		std::shared_ptr<Application> GetApplication(const info::VHostAppName &vhost_app_name) const;
		const info::Application &GetApplicationInfo(const ov::String &vhost_name, info::application_id_t app_id) const;

		std::vector<std::shared_ptr<VirtualHost>> GetVirtualHostList() const;
		std::vector<Module> GetModuleList() const;

		bool GetUrlListForLocation(const info::VHostAppName &vhost_app_name, const ov::String &host_name, const ov::String &stream_name, Origin &matched_origin, std::vector<ov::String> &url_list);

		// Server Info
		std::shared_ptr<const cfg::Server> 	_server_config;

		std::shared_ptr<MediaRouterInterface> _media_router;

		std::atomic<info::application_id_t> _last_application_id{info::MinApplicationId};

		// Modules
		std::vector<Module> _module_list;
		mutable std::shared_mutex _module_list_mutex;

		// key: vhost_name
		std::map<ov::String, std::shared_ptr<VirtualHost>> _virtual_host_map;
		// ordered vhost list
		std::vector<std::shared_ptr<VirtualHost>> _virtual_host_list;
		mutable std::shared_mutex _virtual_host_mutex;

		std::shared_ptr<pvd::Stream> GetProviderStream(const info::VHostAppName &vhost_app_name, const ov::String &stream_name);

		// Module Timer : It is called periodically by the timer
		ov::DelayQueue _timer{"Orchestrator"};
	};
}  // namespace ocst
