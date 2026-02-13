//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2023 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <base/ovlibrary/ovlibrary.h>
#include <openssl/err.h>
#include <openssl/ocsp.h>
#include <openssl/ssl.h>
#include <openssl/store.h>

#include <memory>

namespace ov
{
	class OcspContext
	{
	public:
		static std::shared_ptr<OcspContext> Create(SSL_CTX *ssl_ctx, X509 *cert);

		OcspContext(SSL_CTX *ssl_ctx, X509 *cert);

		// Get the responder URL from certificate
		bool ParseOcspUrl(X509 *cert);

		// Create a OCSP query for server certificate
		bool Request(X509 *cert);

		bool HasResponse() const;
		const ov::RaiiPtr<OCSP_RESPONSE> &GetResponse();
		int GetResponseCode() const;

		bool IsStatusRequestEnabled() const
		{
			return _status_request_enabled;
		}

		static bool IsStatusRequestEnabled(const X509 *cert);

		bool IsExpired(int millisecond_time = (4 * 60 * 60 * 1000)) const
		{
			if (IsStatusRequestEnabled() == false)
			{
				// Never expire if OCSP stapling is disabled
				return false;
			}

			auto delta = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now() - _received_time);

			return (delta.count() >= millisecond_time);
		}

	private:
		// A wrapper of OPENSSL_free() (OPENSSL_free() is a macro and cannot be used as a function pointer)
		static void OpensslFree(char *instance)
		{
			::OPENSSL_free(instance);
		}

		static bool FindStatusRequestExtension(X509_EXTENSION *ext);

		bool HandleExtensions(SSL *ssl, OCSP_REQUEST *request);

		// Called by OSSL_HTTP_transfer()
		BIO *OnHttpTlsResponse(BIO *bio, int connect, int detail);

		// Called by OSSL_HTTP_transfer()
		static BIO *OnHttpTlsResponse(BIO *bio, void *arg, int connect, int detail)
		{
			return static_cast<OcspContext *>(arg)->OnHttpTlsResponse(bio, connect, detail);
		}

	private:
		bool _status_request_enabled = false;

		std::string _url;

		ov::RaiiPtr<char> _host = {nullptr, OpensslFree};
		ov::RaiiPtr<char> _port = {nullptr, OpensslFree};
		ov::RaiiPtr<char> _path = {nullptr, OpensslFree};

		bool _use_tls = false;

		SSL_CTX *_ssl_ctx = nullptr;
		// Used to request to OCSP responder if the responder URL is HTTPS
		ov::RaiiPtr<SSL_CTX> _ssl_request_ctx = {nullptr, ::SSL_CTX_free};

		std::chrono::time_point<std::chrono::system_clock> _received_time;

		ov::RaiiPtr<OCSP_RESPONSE> _response = {nullptr, ::OCSP_RESPONSE_free};
		// Last error code generated when querying OCSP responder
		int _response_code = -1;
	};
}  // namespace ov