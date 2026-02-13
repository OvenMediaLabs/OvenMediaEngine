//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2020 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <base/info/host.h>
#include <base/ovlibrary/ovlibrary.h>
#include <base/ovsocket/ovsocket.h>
#include <config/config.h>
#include <modules/http/cors/cors_manager.h>
#include <modules/http/server/http_server_manager.h>

#include "controllers/root_controller.h"

namespace api
{
	class Server : public ov::EnableSharedFromThis<Server>
	{
	public:
		bool Start(const std::shared_ptr<const cfg::Server> &server_config);
		bool Stop();

		MAY_THROWS(http::HttpError)
		void CreateVHost(const cfg::vhost::VirtualHost &vhost_config);
		MAY_THROWS(http::HttpError)
		void DeleteVHost(const info::Host &host_info);

		std::shared_ptr<const http::HttpError> ReloadCertificate();

	protected:
		std::shared_ptr<info::Certificate> CreateCertificate();

		bool PrepareHttpServers(
			const std::vector<ov::String> &server_ip_list,
			const bool is_port_configured, const uint16_t port,
			const bool is_tls_port_configured, const uint16_t tls_port,
			const int worker_count);

		bool SetupCORS(const cfg::mgr::api::API &api_config);
		bool SetupAccessToken(const cfg::mgr::api::API &api_config);

		std::shared_ptr<http::svr::RequestInterceptor> CreateInterceptor();

	protected:
		std::shared_ptr<const cfg::Server> _server_config;

		// For HTTPS certificate reloading
		std::optional<cfg::cmn::Host> _host_config;

		std::mutex _http_server_list_mutex;
		std::vector<std::shared_ptr<http::svr::HttpServer>> _http_server_list;
		std::vector<std::shared_ptr<http::svr::HttpsServer>> _https_server_list;

		std::shared_ptr<RootController> _root_controller;

		ov::String _access_token;

		bool _is_storage_path_initialized = false;
		ov::String _storage_path;

		http::CorsManager _cors_manager;
	};
}  // namespace api
