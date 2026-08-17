//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2021 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include "../certificate.h"
#include "./ocsp_handler.h"
#include "./openssl_error.h"
#include "./tls_context_callback.h"

namespace ov
{
	enum class TlsMethod
	{
		// DTLS_server_method()
		DTls,
		// TLS_server_method() /
		Tls
	};

	// A wrapper of SSL_CTX
	class TlsContext
	{
	public:
		~TlsContext();

		static std::shared_ptr<TlsContext> CreateServerContext(
			TlsMethod method,
			const std::shared_ptr<const ::Certificate> &certificate,
			const ov::String &cipher_list,
			bool enable_h2_alpn,
			bool enable_ocsp_staping,
			const ov::TlsContextCallback *callback,
			// output param
			std::shared_ptr<const ov::Error> *error);

		static std::shared_ptr<TlsContext> CreateClientContext(
			// output param
			std::shared_ptr<const ov::Error> *error);

		const SSL_CTX *GetSslContext() const noexcept
		{
			return _ssl_ctx;
		}

		SSL_CTX *GetSslContext() noexcept
		{
			return _ssl_ctx;
		}

		bool UseSslContext(SSL *ssl);

		void SetVerify(int mode);

	protected:
		MAY_THROWS(ov::OpensslError)
		void Prepare(
			const SSL_METHOD *method,
			const std::shared_ptr<const Certificate> &certificate,
			const ov::String &cipher_list,
			bool enable_h2_alpn,
			bool enable_ocsp_staping,
			const TlsContextCallback *callback);

		MAY_THROWS(ov::OpensslError)
		void Prepare(
			const SSL_METHOD *method,
			const TlsContextCallback *callback);

		template <typename Treturn, Treturn default_value, class Tmember, Tmember member, typename... Targuments>
		static Treturn DoCallback(void *tls_context, Targuments... args)
		{
			if (tls_context == nullptr)
			{
				OV_ASSERT2(false);
				return default_value;
			}

			auto tls = static_cast<TlsContext *>(tls_context);
			auto &target = tls->_callback.*member;

			if (target != nullptr)
			{
				return target(tls, args...);
			}

			return default_value;
		}

		static int OnServerNameCallback(SSL *s, int *ad, void *arg);
		int OnServerName(SSL *ssl);

		static int OnALPNSelectCallback(SSL *ssl, const unsigned char **out, unsigned char *outlen, const unsigned char *in, unsigned int inlen, void *arg);
		static bool SelectALPNProtocol(ov::String key, const unsigned char **out, unsigned char *outlen, const unsigned char *in, unsigned int inlen);

		MAY_THROWS(ov::OpensslError)
		void SetCertificate(const std::shared_ptr<const ::Certificate> &certificate);

		static int TlsVerify(X509_STORE_CTX *store, void *arg);

	protected:
		SSL_CTX *_ssl_ctx = nullptr;
		bool _h2_alpn_enabled = true;
		TlsContextCallback _callback;

		OcspHandler _ocsp_handler;
	};
}  // namespace ov
