//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2020 AirenSoft. All rights reserved.
//
//==============================================================================
#include "api_server.h"

#include <base/info/ome_version.h>
#include <orchestrator/orchestrator.h>
#include <sys/utsname.h>

#include "api_private.h"
#include "controllers/root_controller.h"

#define API_VERSION "1"

namespace api
{
	struct XmlWriter : pugi::xml_writer
	{
		ov::String result;

		void write(const void *data, size_t size) override
		{
			result.Append(static_cast<const char *>(data), size);
		}
	};

	bool Server::SetupCORS(const cfg::mgr::api::API &api_config)
	{
		bool is_cors_parsed;
		auto cross_domains = api_config.GetCrossDomains(&is_cors_parsed);

		if (is_cors_parsed)
		{
			// API server doesn't have VHost, so use dummy VHost
			auto vhost_app_name = info::VHostAppName::InvalidVHostAppName();
			_cors_manager.SetCrossDomains(vhost_app_name, cross_domains);
		}

		return true;
	}

	bool Server::SetupAccessToken(const cfg::mgr::api::API &api_config)
	{
		_access_token = api_config.GetAccessToken();

		if (_access_token.IsEmpty())
		{
#if DEBUG
			logtw("An empty <AccessToken> setting was found. This is only allowed on Debug builds for ease of development, and the Release build does not allow empty <AccessToken>.");
#else	// DEBUG
			logte("Empty <AccessToken> is not allowed");
			return false;
#endif	// DEBUG
		}

		return true;
	}

	std::shared_ptr<info::Certificate> Server::CreateCertificate()
	{
		if (_host_config.has_value() == false)
		{
			return nullptr;
		}

		const auto &host_config = _host_config.value();

		return info::Certificate::CreateCertificate(
			"api_server",
			host_config.GetNameList(),
			host_config.GetTls());
	}

	bool Server::PrepareHttpServers(
		const std::vector<ov::String> &ip_list,
		const bool is_port_configured, const uint16_t port,
		const bool is_tls_port_configured, const uint16_t tls_port,
		const int worker_count)
	{
		auto http_server_manager = http::svr::HttpServerManager::GetInstance();

		std::vector<std::shared_ptr<http::svr::HttpServer>> http_server_list;
		std::vector<std::shared_ptr<http::svr::HttpsServer>> https_server_list;

		auto http_interceptor						   = CreateInterceptor();
		std::shared_ptr<info::Certificate> certificate = CreateCertificate();

		if (http_server_manager->CreateServers(
				"API Server", "APISvr",
				&http_server_list, &https_server_list,
				ip_list,
				is_port_configured, port,
				is_tls_port_configured, tls_port,
				certificate,
				false,
				[&](const ov::SocketAddress &address, bool is_https, const std::shared_ptr<http::svr::HttpServer> &http_server) {
					http_server->AddInterceptor(http_interceptor);
				},
				worker_count))
		{
			std::lock_guard<std::mutex> lock(_http_server_list_mutex);

			_http_server_list  = std::move(http_server_list);
			_https_server_list = std::move(https_server_list);

			return true;
		}

		http_server_manager->ReleaseServers(&http_server_list);
		http_server_manager->ReleaseServers(&https_server_list);

		return false;
	}

	bool Server::Start(const std::shared_ptr<const cfg::Server> &server_config)
	{
		// API Server configurations
		const auto &managers_config = server_config->GetManagers();
		const auto &api_config		= managers_config.GetApi();

		// Port configurations
		const auto &api_bind_config = server_config->GetBind().GetManagers().GetApi();

		if (api_bind_config.IsParsed() == false)
		{
			logti("API Server is disabled by configuration");
			return true;
		}

		bool is_configured;
		auto worker_count = api_bind_config.GetWorkerCount(&is_configured);
		worker_count	  = is_configured ? worker_count : HTTP_SERVER_USE_DEFAULT_COUNT;

		bool is_port_configured;
		auto &port_config = api_bind_config.GetPort(&is_port_configured);

		bool is_tls_port_configured;
		auto &tls_port_config = api_bind_config.GetTlsPort(&is_tls_port_configured);

		if ((is_port_configured == false) && (is_tls_port_configured == false))
		{
			logtw("API Server is disabled - No port is configured");
			return true;
		}

		_host_config = is_tls_port_configured
						   ? managers_config.GetHost()
						   : static_cast<std::optional<cfg::cmn::Host>>(std::nullopt);

		auto result	 = SetupCORS(api_config) &&
					  SetupAccessToken(api_config) &&
					  PrepareHttpServers(
						  server_config->GetIPList(),
						  is_port_configured, port_config.GetPort(),
						  is_tls_port_configured, tls_port_config.GetPort(),
						  worker_count);

		if (result)
		{
			_server_config = server_config;
		}
		else
		{
			_host_config.reset();
		}

		return result;
	}

	std::shared_ptr<http::svr::RequestInterceptor> Server::CreateInterceptor()
	{
		auto http_interceptor = std::make_shared<http::svr::DefaultInterceptor>();

		// CORS header processor
		http_interceptor->Register(http::Method::All, R"(.+)", [=](const std::shared_ptr<http::svr::HttpExchange> &client) -> http::svr::NextHandler {
			auto response = client->GetResponse();
			auto request  = client->GetRequest();

			// Set default headers
			response->SetHeader("Server", "OvenMediaEngine");
			response->SetHeader("Content-Type", "text/html");

			// API Server uses OPTIONS/GET/POST/PUT/PATCH/DELETE methods
			_cors_manager.SetupHttpCorsHeader(
				info::VHostAppName::InvalidVHostAppName(),
				request, response,
				{http::Method::Options, http::Method::Head, http::Method::Get, http::Method::Post, http::Method::Put, http::Method::Patch, http::Method::Delete});

			return http::svr::NextHandler::Call;
		});

		// Preflight request processor
		http_interceptor->Register(http::Method::Options, R"(.+)", [=](const std::shared_ptr<http::svr::HttpExchange> &client) -> http::svr::NextHandler {
			// Respond 204 No Content for preflight request
			client->GetResponse()->SetStatusCode(http::StatusCode::NoContent);

			// Do not call the next handler to prevent 404 Not Found
			return http::svr::NextHandler::DoNotCall;
		});

		// Request Handlers will be added to http_interceptor
		_root_controller = std::make_shared<RootController>(_access_token);
		_root_controller->SetServer(GetSharedPtr());
		_root_controller->SetInterceptor(http_interceptor);
		_root_controller->PrepareHandlers();

		return http_interceptor;
	}

	bool Server::Stop()
	{
		auto manager = http::svr::HttpServerManager::GetInstance();

		_http_server_list_mutex.lock();
		auto http_server_list  = std::move(_http_server_list);
		auto https_server_list = std::move(_https_server_list);
		_http_server_list_mutex.unlock();

		auto http_result			 = manager->ReleaseServers(&http_server_list);
		auto https_result			 = manager->ReleaseServers(&https_server_list);

		_is_storage_path_initialized = false;
		_storage_path				 = "";

		_root_controller			 = nullptr;

		return http_result && https_result;
	}

	void Server::CreateVHost(const cfg::vhost::VirtualHost &vhost_config)
	{
		OV_ASSERT2(vhost_config.IsReadOnly() == false);

		switch (ocst::Orchestrator::GetInstance()->CreateVirtualHost(vhost_config))
		{
			case ocst::Result::Failed:
				throw http::HttpError(http::StatusCode::BadRequest,
									  "Failed to create the virtual host: [%s]",
									  vhost_config.GetName().CStr());

			case ocst::Result::Succeeded:
				break;

			case ocst::Result::Exists:
				throw http::HttpError(http::StatusCode::Conflict,
									  "The virtual host already exists: [%s]",
									  vhost_config.GetName().CStr());

			case ocst::Result::NotExists:
				// CreateVirtualHost() never returns NotExists
				OV_ASSERT2(false);
				throw http::HttpError(http::StatusCode::InternalServerError,
									  "Unknown error occurred: [%s]",
									  vhost_config.GetName().CStr());
		}
	}

	void Server::DeleteVHost(const info::Host &host_info)
	{
		OV_ASSERT2(host_info.IsReadOnly() == false);

		logti("Deleting virtual host: %s", host_info.GetName().CStr());

		switch (ocst::Orchestrator::GetInstance()->DeleteVirtualHost(host_info))
		{
			case ocst::Result::Failed:
				throw http::HttpError(http::StatusCode::InternalServerError,
									  "Failed to delete the virtual host: [%s]",
									  host_info.GetName().CStr());

			case ocst::Result::Succeeded:
				break;

			case ocst::Result::Exists:
				// CreateVirtDeleteVirtualHost() never returns Exists
				OV_ASSERT2(false);
				throw http::HttpError(http::StatusCode::InternalServerError,
									  "Unknown error occurred: [%s]",
									  host_info.GetName().CStr());

			case ocst::Result::NotExists:
				// CreateVirtualHost() never returns NotExists
				throw http::HttpError(http::StatusCode::NotFound,
									  "The virtual host not exists: [%s]",
									  host_info.GetName().CStr());
		}
	}

	std::shared_ptr<const http::HttpError> Server::ReloadCertificate()
	{
		if (
			(_host_config.has_value() == false) ||
			(_host_config->GetTls().IsParsed() == false))
		{
			return http::HttpError::CreateError(http::StatusCode::BadRequest, "TLS is not enabled for the API Server");
		}

		auto certificate = CreateCertificate();

		if (certificate == nullptr)
		{
			return http::HttpError::CreateError(http::StatusCode::InternalServerError, "Failed to create a certificate for the API Server");
		}

		std::shared_ptr<const http::HttpError> first_error;

		std::vector<std::shared_ptr<http::svr::HttpsServer>> https_server_list;
		{
			std::lock_guard<std::mutex> lock(_http_server_list_mutex);
			https_server_list = _https_server_list;
		}
		for (auto &https_server : https_server_list)
		{
			auto error = https_server->InsertCertificate(certificate);

			if (error != nullptr)
			{
				logte("Could not reload certificate for API Server: %s", error->What());

				first_error = (first_error == nullptr)
								  ? http::HttpError::CreateError(http::StatusCode::InternalServerError, error->What())
								  : first_error;
			}
		}

		return first_error;
	}
}  // namespace api
