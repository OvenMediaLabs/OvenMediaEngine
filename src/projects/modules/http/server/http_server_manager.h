//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2020 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <base/info/certificate.h>

#include "http_server.h"
#include "https_server.h"
#include "config/items/common/cross_domains.h"

namespace http
{
	namespace svr
	{
		class HttpServerManager : public ov::Singleton<HttpServerManager>
		{
		public:
			using HttpServerCreationCallback = std::function<void(const ov::SocketAddress &address, bool is_https, const std::shared_ptr<HttpServer> &http_server)>;

		public:
			std::shared_ptr<HttpServer> CreateHttpServer(const char *server_name, const char *server_short_name, const ov::SocketAddress &address, int worker_count = HTTP_SERVER_USE_DEFAULT_COUNT);

			void SetDefaultCrosssDomains(const cfg::cmn::CrossDomains &cross_domain_cfg);
			const http::CorsManager &GetDefaultCorsManager() const;

			bool AppendCertificate(const ov::SocketAddress &address, const std::shared_ptr<const info::Certificate> &certificate);
			bool RemoveCertificate(const ov::SocketAddress &address, const std::shared_ptr<const info::Certificate> &certificate);

			std::shared_ptr<HttpsServer> CreateHttpsServer(const char *server_name, const char *server_short_name, const ov::SocketAddress &address, const std::shared_ptr<const info::Certificate> &certificate, bool disable_http2_force, int worker_count);
			std::shared_ptr<HttpsServer> CreateHttpsServer(const char *server_name, const char *server_short_name, const ov::SocketAddress &address, bool disable_http2_force, int worker_count = HTTP_SERVER_USE_DEFAULT_COUNT);

			bool CreateServers(
				const char *server_name, const char *server_short_name,
				std::vector<std::shared_ptr<HttpServer>> *http_server_list,
				std::vector<std::shared_ptr<HttpsServer>> *https_server_list,
				const std::vector<ov::String> &server_ip_list,
				bool is_port_configured, const uint16_t port,
				bool is_tls_port_configured, const uint16_t tls_port,
				std::shared_ptr<const info::Certificate> certificate,
				bool disable_http2_force,
				HttpServerCreationCallback creation_callback = nullptr,
				int worker_count = HTTP_SERVER_USE_DEFAULT_COUNT);

			std::shared_ptr<HttpsServer> GetHttpsServer(const ov::SocketAddress &address);
			bool ReleaseServer(const std::shared_ptr<HttpServer> &http_server);

			template <typename T>
			bool ReleaseServers(std::vector<std::shared_ptr<T>> *server_list)
			{
				bool failed = false;

				for (auto &server : *server_list)
				{
					if (ReleaseServer(server) == false)
					{
						failed = true;
					}
				}

				return (failed == false);
			}

		protected:
			std::mutex _http_servers_mutex;
			std::map<ov::SocketAddress, std::shared_ptr<HttpServer>> _http_servers;

			http::CorsManager _default_cors_manager;
		};
	}  // namespace svr
}  // namespace http
