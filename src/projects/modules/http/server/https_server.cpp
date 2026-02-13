//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================
#include "https_server.h"

#include "./http_server_private.h"

// Reference: https://wiki.mozilla.org/Security/Server_Side_TLS

// Modern compatibility
#define HTTP_MODERN_COMPATIBILITY "ECDHE-ECDSA-AES256-GCM-SHA384:ECDHE-RSA-AES256-GCM-SHA384:ECDHE-ECDSA-CHACHA20-POLY1305:ECDHE-RSA-CHACHA20-POLY1305:ECDHE-ECDSA-AES128-GCM-SHA256:ECDHE-RSA-AES128-GCM-SHA256:ECDHE-ECDSA-AES256-SHA384:ECDHE-RSA-AES256-SHA384:ECDHE-ECDSA-AES128-SHA256:ECDHE-RSA-AES128-SHA256"
// Intermediate compatibility
#define HTTP_INTERMEDIATE_COMPATIBILITY "ECDHE-ECDSA-CHACHA20-POLY1305:ECDHE-RSA-CHACHA20-POLY1305:ECDHE-ECDSA-AES128-GCM-SHA256:ECDHE-RSA-AES128-GCM-SHA256:ECDHE-ECDSA-AES256-GCM-SHA384:ECDHE-RSA-AES256-GCM-SHA384:DHE-RSA-AES128-GCM-SHA256:DHE-RSA-AES256-GCM-SHA384:ECDHE-ECDSA-AES128-SHA256:ECDHE-RSA-AES128-SHA256:ECDHE-ECDSA-AES128-SHA:ECDHE-RSA-AES256-SHA384:ECDHE-RSA-AES128-SHA:ECDHE-ECDSA-AES256-SHA384:ECDHE-ECDSA-AES256-SHA:ECDHE-RSA-AES256-SHA:DHE-RSA-AES128-SHA256:DHE-RSA-AES128-SHA:DHE-RSA-AES256-SHA256:DHE-RSA-AES256-SHA:ECDHE-ECDSA-DES-CBC3-SHA:ECDHE-RSA-DES-CBC3-SHA:EDH-RSA-DES-CBC3-SHA:AES128-GCM-SHA256:AES256-GCM-SHA384:AES128-SHA256:AES256-SHA256:AES128-SHA:AES256-SHA:DES-CBC3-SHA:!DSS"
// Backward compatibility
#define HTTP_BACKWARD_COMPATIBILITY "ECDHE-ECDSA-CHACHA20-POLY1305:ECDHE-RSA-CHACHA20-POLY1305:ECDHE-RSA-AES128-GCM-SHA256:ECDHE-ECDSA-AES128-GCM-SHA256:ECDHE-RSA-AES256-GCM-SHA384:ECDHE-ECDSA-AES256-GCM-SHA384:DHE-RSA-AES128-GCM-SHA256:DHE-DSS-AES128-GCM-SHA256:kEDH+AESGCM:ECDHE-RSA-AES128-SHA256:ECDHE-ECDSA-AES128-SHA256:ECDHE-RSA-AES128-SHA:ECDHE-ECDSA-AES128-SHA:ECDHE-RSA-AES256-SHA384:ECDHE-ECDSA-AES256-SHA384:ECDHE-RSA-AES256-SHA:ECDHE-ECDSA-AES256-SHA:DHE-RSA-AES128-SHA256:DHE-RSA-AES128-SHA:DHE-DSS-AES128-SHA256:DHE-RSA-AES256-SHA256:DHE-DSS-AES256-SHA:DHE-RSA-AES256-SHA:ECDHE-RSA-DES-CBC3-SHA:ECDHE-ECDSA-DES-CBC3-SHA:EDH-RSA-DES-CBC3-SHA:AES128-GCM-SHA256:AES256-GCM-SHA384:AES128-SHA256:AES256-SHA256:AES128-SHA:AES256-SHA:AES:DES-CBC3-SHA:HIGH:SEED:!aNULL:!eNULL:!EXPORT:!DES:!RC4:!MD5:!PSK:!RSAPSK:!aDH:!aECDH:!EDH-DSS-DES-CBC3-SHA:!KRB5-DES-CBC3-SHA:!SRP"
// Fastest suite only, which is still considered `secure`.
#define HTTP_FAST_NOT_VERY_SECURE "AES128-SHA"

namespace http
{
	namespace svr
	{
		std::shared_ptr<const ov::Error> HttpsServer::InsertCertificate(const std::shared_ptr<const info::Certificate> &certificate)
		{
			if (certificate == nullptr)
			{
				return std::make_shared<ov::OpensslError>("Certificate is nullptr");
			}

			ov::TlsContextCallback tls_context_callback = {
				.create_callback = nullptr,
				.verify_callback = nullptr,
				.sni_callback = std::bind(&HttpsServer::HandleSniCallback, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3)};

			std::shared_ptr<const ov::Error> error;
			auto tls_context = ov::TlsContext::CreateServerContext(
				ov::TlsMethod::Tls, certificate->GetCertificate(),
				HTTP_FAST_NOT_VERY_SECURE,
				IsHttp2Enabled(),
				true,
				&tls_context_callback,
				&error);

			if (tls_context == nullptr)
			{
				return error;
			}

			std::lock_guard lock_guard(_https_certificate_map_mutex);

			logtt("Append the certificate for host: %s", certificate->ToString().CStr());

			// remove the certificate if it already exists
			_https_certificate_map.erase(certificate->GetName());
			_https_certificate_map.emplace(certificate->GetName(), std::make_shared<HttpsCertificate>(certificate, tls_context));

			return nullptr;
		}

		std::shared_ptr<const ov::Error> HttpsServer::RemoveCertificate(const std::shared_ptr<const info::Certificate> &certificate)
		{
			std::lock_guard lock_guard(_https_certificate_map_mutex);

			logtt("Remove the certificate for host: %s", certificate->ToString().CStr());

			_https_certificate_map.erase(certificate->GetName());

			return nullptr;
		}

		// Deprecated
		std::shared_ptr<const ov::Error> HttpsServer::AppendCertificateList(const std::vector<std::shared_ptr<const info::Certificate>> &certificate_list)
		{
			// for (auto &certificate : certificate_list)
			// {
			// 	auto error = AppendCertificate(certificate);

			// 	if (error != nullptr)
			// 	{
			// 		return error;
			// 	}
			// }

			return nullptr;
		}

		void HttpsServer::OnConnected(const std::shared_ptr<ov::Socket> &remote)
		{
			std::shared_ptr<HttpsCertificate> https_certificate;

			{
				std::lock_guard lock_guard(_https_certificate_map_mutex);
				if (_https_certificate_map.empty())
				{
					logte("Could not handle connection event: there is no certificate");
					return;
				}

				// Eventually this certificate will be ignored after SNI works.
				// - SSL_accept() requires an SSL instance
				// - SSL_CTX is required to create an SSL instance
				// - SSL_CTX contains certificate information.
				// - An empty SSL_CTX could be used, but we just used the first certificate
				https_certificate = _https_certificate_map.begin()->second;
			}

			auto client = ProcessConnect(remote);

			if (client == nullptr)
			{
				return;
			}

			auto tls_data = std::make_shared<ov::TlsServerData>(https_certificate->tls_context, true);

			tls_data->SetWriteCallback([remote](const void *data, size_t length) -> ssize_t {
				return remote->Send(data, length) ? length : -1L;
			});

			client->SetTlsData(tls_data);
		}

		void HttpsServer::OnDataReceived(const std::shared_ptr<ov::Socket> &remote, const ov::SocketAddress &address, const std::shared_ptr<const ov::Data> &data)
		{
			auto connection = FindClient(remote);
			if (connection == nullptr)
			{
				// This can be called in situations where the client closes the connection from the server at the same time as the data is sent
				return;
			}

			auto tls_data = connection->GetTlsData();

			if (tls_data != nullptr)
			{
				std::shared_ptr<const ov::Data> plain_data;

				auto prev_tls_state = tls_data->GetState();

				if (tls_data->Decrypt(data, &plain_data))
				{
					if (prev_tls_state == ov::TlsServerData::State::WaitingForAccept &&
						tls_data->GetState() == ov::TlsServerData::State::Accepted)
					{
						// The client has accepted the connection
						connection->OnTlsAccepted();
					}

					if ((plain_data != nullptr) && (plain_data->GetLength() > 0))
					{
						// plain_data is HTTP data
						// Use the decrypted data
						HttpServer::OnDataReceived(remote, address, plain_data);
					}
					else
					{
						// Need more data to decrypt the data
					}

					return;
				}

				// Error
				logtt("Could not decrypt data");
				remote->Close();
			}
			else
			{
				OV_ASSERT(false, "TlsServerData must not be null");
			}
		}

		bool HttpsServer::HandleSniCallback(ov::TlsContext *tls_context, SSL *ssl, const ov::String &server_name)
		{
			std::shared_ptr<HttpsCertificate> https_certificate;

			{
				std::lock_guard lock_guard(_https_certificate_map_mutex);
				for (auto &[vhost_name, https_cert] : _https_certificate_map)
				{
					if (https_cert->certificate->IsCertificateForHost(server_name))
					{
						https_certificate = https_cert;
						break;
					}
				}
			}

			if (https_certificate == nullptr)
			{
				logtw("Could not find certificate for server_name: %s", server_name.CStr());
				return false;
			}

			auto &certificate = https_certificate->certificate;

			if (https_certificate->tls_context->UseSslContext(ssl))
			{
				logtt("Certification found for server_name: %s, certificate: %s", server_name.CStr(), certificate->ToString().CStr());
				return true;
			}

			logte("Could not set SSL context %s to %p", certificate->ToString().CStr(), ssl);

			return false;
		}
	}  // namespace svr
}  // namespace http
