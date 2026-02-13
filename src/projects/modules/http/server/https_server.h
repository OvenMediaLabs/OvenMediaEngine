//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include "base/info/host.h"
#include "http_server.h"
#include "orchestrator/orchestrator.h"

namespace http
{
	namespace svr
	{
		class HttpsServer : public HttpServer
		{
		public:
			HttpsServer(const char *server_name, const char *server_short_name)
				: HttpServer(server_name, server_short_name)
			{
			}

			std::shared_ptr<const ov::Error> InsertCertificate(const std::shared_ptr<const info::Certificate> &certificate);
			std::shared_ptr<const ov::Error> RemoveCertificate(const std::shared_ptr<const info::Certificate> &certificate);

			// Deprecated
			std::shared_ptr<const ov::Error> AppendCertificateList(const std::vector<std::shared_ptr<const info::Certificate>> &certificate_list);

		protected:
			struct HttpsCertificate
			{
				HttpsCertificate() = default;
				HttpsCertificate(std::shared_ptr<const info::Certificate> certificate, std::shared_ptr<ov::TlsContext> tls_context)
					: certificate(std::move(certificate)),
					  tls_context(std::move(tls_context))
				{
				}

				std::shared_ptr<const info::Certificate> certificate;
				std::shared_ptr<ov::TlsContext> tls_context;
			};

		protected:
			//--------------------------------------------------------------------
			// Implementation of PhysicalPortObserver
			//--------------------------------------------------------------------
			void OnConnected(const std::shared_ptr<ov::Socket> &remote) override;
			void OnDataReceived(const std::shared_ptr<ov::Socket> &remote, const ov::SocketAddress &address, const std::shared_ptr<const ov::Data> &data) override;

		protected:
			bool HandleSniCallback(ov::TlsContext *tls_context, SSL *ssl, const ov::String &server_name);

		protected:
			std::mutex _https_certificate_map_mutex;

			// Certificate Name : HttpsCertificate
			std::map<ov::String, std::shared_ptr<HttpsCertificate>> _https_certificate_map;
		};
	}  // namespace svr
}  // namespace http
