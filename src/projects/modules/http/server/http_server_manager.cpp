//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2020 AirenSoft. All rights reserved.
//
//==============================================================================
#include "http_server_manager.h"

#include <orchestrator/orchestrator.h>

#include "./http_server_private.h"

namespace http
{
	namespace svr
	{
		std::shared_ptr<HttpServer> HttpServerManager::CreateHttpServer(const char *server_name, const char *server_short_name, const ov::SocketAddress &address, int worker_count)
		{
			std::shared_ptr<HttpServer> http_server = nullptr;

			auto module_config = cfg::ConfigManager::GetInstance()->GetServer()->GetModules();
			auto http2_enabled = module_config.GetHttp2().IsEnabled();

			{
				auto lock_guard = std::lock_guard(_http_servers_mutex);
				auto item = _http_servers.find(address);

				if (item != _http_servers.end())
				{
					http_server = item->second;

					// Assume that http_server is not HttpsServer
					auto https_server = std::dynamic_pointer_cast<HttpsServer>(http_server);

					if (https_server != nullptr)
					{
						logte("Cannot reuse instance: Requested Server, but previous instance is HttpsServer (%s)", address.ToString().CStr());
						http_server = nullptr;
					}
					else
					{
						if (worker_count != HTTP_SERVER_USE_DEFAULT_COUNT)
						{
							auto physical_port = http_server->GetPhysicalPort();

							if (physical_port != nullptr)
							{
								if (physical_port->GetWorkerCount() != worker_count)
								{
									logtw("The number of workers in the existing physical port differs from the number of workers passed by the argument: physical port: %d, argument: %d",
										  physical_port->GetWorkerCount(), worker_count);
									logtw("Because worker counts are different, the first initialized count is used: %d", physical_port->GetWorkerCount());
								}
							}
						}
					}
				}
				else
				{
					// Create a new HTTP server
					http_server = std::make_shared<HttpServer>(server_name, server_short_name);

					if (http_server->Start(address, worker_count, http2_enabled))
					{
						_http_servers[address] = http_server;
					}
					else
					{
						// Failed to start
						http_server = nullptr;
					}
				}

				return http_server;
			}
		}

		void HttpServerManager::SetDefaultCrosssDomains(const cfg::cmn::CrossDomains &cross_domain_cfg)
		{
			_default_cors_manager.SetDefaultCrossDomains(cross_domain_cfg);
		}

		const http::CorsManager &HttpServerManager::GetDefaultCorsManager() const
		{
			return _default_cors_manager;
		}

		bool HttpServerManager::AppendCertificate(const ov::SocketAddress &address, const std::shared_ptr<const info::Certificate> &certificate)
		{
			auto https_server = GetHttpsServer(address);
			if (https_server == nullptr)
			{
				logte("Could not find https server(%s) to append certificate", address.ToString(false).CStr());
				return false;
			}

			auto error = https_server->InsertCertificate(certificate);
			if (error != nullptr)
			{
				logte("Could not set certificate to https server(%s) : %s", address.ToString(false).CStr(), error->What());
				return false;
			}

			return true;
		}

		bool HttpServerManager::RemoveCertificate(const ov::SocketAddress &address, const std::shared_ptr<const info::Certificate> &certificate)
		{
			auto https_server = GetHttpsServer(address);
			if (https_server == nullptr)
			{
				logte("Could not find https server(%s) to append certificate", address.ToString(false).CStr());
				return false;
			}

			auto error = https_server->RemoveCertificate(certificate);
			if (error != nullptr)
			{
				logte("Could not set certificate to https server(%s) : %s", address.ToString(false).CStr(), error->What());
				return false;
			}

			return true;
		}

		std::shared_ptr<HttpsServer> HttpServerManager::CreateHttpsServer(const char *server_name, const char *server_short_name, const ov::SocketAddress &address, const std::shared_ptr<const info::Certificate> &certificate, bool disable_http2_force, int worker_count)
		{
			std::shared_ptr<HttpsServer> https_server = nullptr;
			auto module_config = cfg::ConfigManager::GetInstance()->GetServer()->GetModules();
			auto http2_enabled = module_config.GetHttp2().IsEnabled();

			if (disable_http2_force == true)
			{
				http2_enabled = false;
			}

			auto lock_guard = std::lock_guard(_http_servers_mutex);
			auto item = _http_servers.find(address);

			if (item != _http_servers.end())
			{
				auto http_server = item->second;

				// Assume that http_server is not HttpsServer
				https_server = std::dynamic_pointer_cast<HttpsServer>(http_server);
				if (https_server == nullptr)
				{
					logte("Cannot reuse instance of %s: Requested HTTPS Server, but previous instance is HTTP Server (%s)", server_name, address.ToString().CStr());
					return nullptr;
				}

				if (https_server->IsHttp2Enabled() && (http2_enabled == false))
				{
					logtw("Attempting to use HTTP/2 for ports with address %s enabled as HTTP/1.1 only.", address.ToString().CStr());
				}
				else if ((https_server->IsHttp2Enabled() == false) && http2_enabled)
				{
					logtw("The %s address is trying to use HTTP/1.1 on a port that is HTTP/2 enabled.", address.ToString().CStr());
				}
			}
			else
			{
				// Create a new HTTP server
				https_server = std::make_shared<HttpsServer>(server_name, server_short_name);

				if (https_server->Start(address, worker_count, http2_enabled))
				{
					_http_servers[address] = https_server;
				}
				else
				{
					// Failed to start HTTP server
					https_server = nullptr;
				}
			}

			if ((https_server != nullptr) && (certificate != nullptr))
			{
				auto error = https_server->InsertCertificate(certificate);
				if (error != nullptr)
				{
					logte("Could not set certificate: %s", error->What());
					https_server = nullptr;
				}
				else
				{
					// HTTPS server is ready
				}
			}

			return https_server;
		}

		std::shared_ptr<HttpsServer> HttpServerManager::CreateHttpsServer(const char *server_name, const char *server_short_name, const ov::SocketAddress &address, bool disable_http2_force, int worker_count)
		{
			return CreateHttpsServer(server_name, server_short_name, address, nullptr, disable_http2_force, worker_count);
		}

		template <typename T>
		bool CreateServers(
			HttpServerManager *http_manager,
			bool is_https,
			std::vector<std::shared_ptr<T>> *server_list,
			const std::vector<ov::String> &server_ip_list, const uint16_t port,
			std::function<std::shared_ptr<T>(const ov::SocketAddress &address)> creation_function,
			HttpServerManager::HttpServerCreationCallback creation_callback)
		{
			const char *http_server_name = is_https ? "HTTPS" : "HTTP";
			std::vector<std::shared_ptr<T>> server_list_temp;

			for (const auto &server_ip : server_ip_list)
			{
				std::vector<ov::SocketAddress> address_list;
				try
				{
					address_list = ov::SocketAddress::Create(server_ip, port);
				}
				catch (const ov::Error &e)
				{
					logtw("Invalid address: %s, port: %d (%s) - %s", server_ip.CStr(), port, http_server_name, e.What());
					return false;
				}

				for (const auto &address : address_list)
				{
					logtt("Attempting to create %s Server instance on %s...", http_server_name, address.ToString().CStr());

					auto server = creation_function(address);

					if (server != nullptr)
					{
						logtt("%s server is created on %s", http_server_name, address.ToString().CStr());

						if (creation_callback != nullptr)
						{
							creation_callback(address, is_https, server);
						}

						server_list_temp.push_back(server);
					}
					else
					{
						logte("Could not initialize HTTP Server on %s", address.ToString().CStr());
						http_manager->ReleaseServers(&server_list_temp);
						return false;
					}
				}
			}

			server_list->insert(server_list->end(), server_list_temp.begin(), server_list_temp.end());

			return true;
		}

		bool HttpServerManager::CreateServers(
			const char *server_name, const char *server_short_name,
			std::vector<std::shared_ptr<HttpServer>> *http_server_list,
			std::vector<std::shared_ptr<HttpsServer>> *https_server_list,
			const std::vector<ov::String> &server_ip_list,
			bool is_port_configured, const uint16_t port,
			bool is_tls_port_configured, const uint16_t tls_port,
			std::shared_ptr<const info::Certificate> certificate,
			bool disable_http2_force,
			HttpServerCreationCallback creation_callback,
			int worker_count)
		{
			http_server_list->clear();
			https_server_list->clear();

			do
			{
				std::vector<ov::String> address_string_list;
				if (is_port_configured)
				{
					if (http::svr::CreateServers<HttpServer>(
							this,
							false, http_server_list, server_ip_list, port,
							[=](const ov::SocketAddress &address) -> std::shared_ptr<HttpServer> {
								return CreateHttpServer(server_name, server_short_name, address, worker_count);
							},
							[&](const ov::SocketAddress &address, bool is_https, const std::shared_ptr<HttpServer> &http_server) {
								address_string_list.emplace_back(address.ToString());
								if (creation_callback != nullptr)
								{
									creation_callback(address, is_https, http_server);
								}
							}) == false)
					{
						logte("Could not create HTTP Server for %s", server_name);
						break;
					}
				}

				std::vector<ov::String> tls_address_string_list;
				if (is_tls_port_configured)
				{
					if (http::svr::CreateServers<HttpsServer>(
							this,
							true, https_server_list, server_ip_list, tls_port,
							[=](const ov::SocketAddress &address) -> std::shared_ptr<HttpsServer> {
								return CreateHttpsServer(server_name, server_short_name, address, certificate, disable_http2_force, worker_count);
							},
							[&](const ov::SocketAddress &address, bool is_https, const std::shared_ptr<HttpServer> &http_server) {
								tls_address_string_list.emplace_back(address.ToString());
								if (creation_callback != nullptr)
								{
									creation_callback(address, is_https, http_server);
								}
							}) == false)
					{
						logte("Could not create HTTPS Server for %s", server_name);
						break;
					}
				}

				ov::String tls_description;

				if (tls_address_string_list.empty() == false)
				{
					tls_description.Format(
						"%sTLS: %s%s",
						(address_string_list.empty() == false) ? " (" : "",
						ov::String::Join(tls_address_string_list, ", ").CStr(),
						(address_string_list.empty() == false) ? ")" : "");
				}

				logti("%s is listening on %s%s...",
					  server_name,
					  ov::String::Join(address_string_list, ", ").CStr(),
					  tls_description.CStr());

				return true;
			} while (false);

			ReleaseServers(http_server_list);
			http_server_list->clear();

			ReleaseServers(https_server_list);
			https_server_list->clear();

			return false;
		}

		bool HttpServerManager::ReleaseServer(const std::shared_ptr<HttpServer> &http_server)
		{
			// TODO(dimiden): Need to implement release feature (by reference count)
			if (http_server != nullptr)
			{
				return http_server->Stop();
			}

			return false;
		}

		std::shared_ptr<HttpsServer> HttpServerManager::GetHttpsServer(const ov::SocketAddress &address)
		{
			std::shared_ptr<HttpsServer> https_server = nullptr;

			auto lock_guard = std::lock_guard(_http_servers_mutex);
			auto item = _http_servers.find(address);

			if (item != _http_servers.end())
			{
				auto http_server = item->second;

				// Assume that http_server is not HttpsServer
				https_server = std::dynamic_pointer_cast<HttpsServer>(http_server);
				if (https_server == nullptr)
				{
					logte("Cannot reuse instance: Requested HttpsServer, but previous instance is Server (%s)", address.ToString().CStr());
				}
			}

			return https_server;
		}
	}  // namespace svr
}  // namespace http
