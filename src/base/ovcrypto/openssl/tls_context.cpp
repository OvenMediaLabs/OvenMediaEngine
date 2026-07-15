//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2021 AirenSoft. All rights reserved.
//
//==============================================================================
#include "tls_context.h"

#include "./openssl_private.h"
#include "./tls.h"

#define DO_CALLBACK_IF_AVAILABLE(return_type, default_value, tls_context, callback_name, ...) \
	DoCallback<return_type, default_value, decltype(&TlsContextCallback::callback_name), &TlsContextCallback::callback_name>(tls_context, ##__VA_ARGS__)

namespace ov
{
	TlsContext::~TlsContext()
	{
		OV_SAFE_FUNC(_ssl_ctx, nullptr, ::SSL_CTX_free, );

		_callback = {};
	}

	std::shared_ptr<TlsContext> TlsContext::CreateServerContext(
		TlsMethod method,
		const std::shared_ptr<const ::Certificate> &certificate,
		const ov::String &cipher_list,
		bool enable_h2_alpn,
		bool enable_ocsp_staping,
		const ov::TlsContextCallback *callback,
		std::shared_ptr<const ov::Error> *error)
	{
		const SSL_METHOD *ssl_method = (method == TlsMethod::Tls) ? ::TLS_server_method() : ::DTLS_server_method();

		auto context = std::make_shared<TlsContext>();

		try
		{
			context->Prepare(
				ssl_method,
				certificate,
				cipher_list,
				enable_h2_alpn,
				enable_ocsp_staping,
				callback);
		}
		catch (const OpensslError &e)
		{
			if (error != nullptr)
			{
				*error = std::make_shared<OpensslError>(e);
			}

			return nullptr;
		}

		return context;
	}

	std::shared_ptr<TlsContext> TlsContext::CreateClientContext(
		std::shared_ptr<const ov::Error> *error)
	{
		auto context = std::make_shared<TlsContext>();

		try
		{
			context->Prepare(TLS_client_method(), nullptr);
		}
		catch (const OpensslError &e)
		{
			if (error != nullptr)
			{
				*error = std::make_shared<OpensslError>(e);
			}

			return nullptr;
		}

		return context;
	}

	void TlsContext::Prepare(
		const SSL_METHOD *method,
		const std::shared_ptr<const Certificate> &certificate,
		const ov::String &cipher_list,
		bool enable_h2_alpn,
		bool enable_ocsp_staping,
		const TlsContextCallback *callback)
	{
		_h2_alpn_enabled = enable_h2_alpn;

		do
		{
			OV_ASSERT2(_ssl_ctx == nullptr);

			if (certificate == nullptr)
			{
				OV_ASSERT2(certificate != nullptr);

				throw OpensslError("Invalid TLS certificate");
			}

			// Create a new SSL session
			decltype(_ssl_ctx) ssl_ctx(::SSL_CTX_new(method));

			if (ssl_ctx == nullptr)
			{
				throw OpensslError("Cannot create SSL context");
			}

			if (callback != nullptr)
			{
				_callback = *callback;
			}

			// Register peer certificate verification callback
			if (_callback.verify_callback != nullptr)
			{
				::SSL_CTX_set_cert_verify_callback(ssl_ctx, TlsVerify, this);
			}
			else
			{
				// Use default
			}

			// https://curl.haxx.se/docs/ssl-ciphers.html
			// https://wiki.mozilla.org/Security/Server_Side_TLS
			::SSL_CTX_set_cipher_list(ssl_ctx, cipher_list.CStr());

			if (enable_h2_alpn == true)
			{
				// Now, only enable TLS 1.3 for HTTP/2
				::SSL_CTX_set_max_proto_version(ssl_ctx, TLS1_3_VERSION);
			}
			else
			{
				// Disable TLS1.3 because it is not yet supported properly by the HTTP server implementation (HTTP2 support, Session tickets, ...)
				// This also allows for using less secure cipher suites for lower CPU requirements when using HLS/DASH/LL-DASH streaming
				::SSL_CTX_set_max_proto_version(ssl_ctx, TLS1_2_VERSION);
			}
			// Disable old TLS versions which are neither secure nor needed any more
			::SSL_CTX_set_min_proto_version(ssl_ctx, TLS1_2_VERSION);

			_ssl_ctx = std::move(ssl_ctx);

			bool result = DO_CALLBACK_IF_AVAILABLE(bool, true, this, create_callback, static_cast<SSL_CTX *>(ssl_ctx));

			if (result == false)
			{
				_callback = {};

				OV_SAFE_FUNC(_ssl_ctx, nullptr, ::SSL_CTX_free, );
				throw OpensslError("An error occurred inside create callback");
			}

			if (certificate != nullptr)
			{
				try
				{
					SetCertificate(certificate);

					if (enable_ocsp_staping)
					{
						// Use OCSP stapling
						_ocsp_handler.Setup(ssl_ctx);
					}
				}
				catch (const OpensslError &error)
				{
					_callback = {};

					OV_SAFE_FUNC(_ssl_ctx, nullptr, ::SSL_CTX_free, );
					throw OpensslError("An error occurred inside create callback: %s", error.What());
				}
			}

			if (_callback.sni_callback != nullptr)
			{
				// Use SNI
				::SSL_CTX_set_tlsext_servername_callback(_ssl_ctx, OnServerNameCallback);
				::SSL_CTX_set_tlsext_servername_arg(_ssl_ctx, this);
			}

			// Use ALPN
			::SSL_CTX_set_alpn_select_cb(_ssl_ctx, OnALPNSelectCallback, this);

		} while (false);
	}

	void TlsContext::Prepare(
		const SSL_METHOD *method,
		const TlsContextCallback *callback)
	{
		do
		{
			OV_ASSERT2(_ssl_ctx == nullptr);

			// Create a new SSL session
			decltype(_ssl_ctx) ctx(::SSL_CTX_new(method));

			if (ctx == nullptr)
			{
				logte("Cannot create SSL context");
				break;
			}

			if (callback != nullptr)
			{
				_callback = *callback;
			}

			// Register peer certificate verification callback
			if (_callback.verify_callback != nullptr)
			{
				::SSL_CTX_set_cert_verify_callback(ctx, TlsVerify, this);
			}
			else
			{
				// Use default
			}

			_ssl_ctx = std::move(ctx);

			bool result = DO_CALLBACK_IF_AVAILABLE(bool, true, this, create_callback, static_cast<SSL_CTX *>(ctx));

			if (result == false)
			{
				_callback = {};

				OV_SAFE_FUNC(_ssl_ctx, nullptr, ::SSL_CTX_free, );
				throw OpensslError("An error occurred inside create callback");
			}
		} while (false);
	}

	int TlsContext::OnALPNSelectCallback(SSL *ssl, const unsigned char **out, unsigned char *outlen, const unsigned char *in, unsigned int inlen, void *arg)
	{
		// arg to TlsContext instance
		auto parent = static_cast<TlsContext *>(arg);

		// h2 first,
		if (parent->_h2_alpn_enabled)
		{
			if (SelectALPNProtocol("h2", out, outlen, in, inlen) == true)
			{
				return SSL_TLSEXT_ERR_OK;
			}
		}

		if (SelectALPNProtocol("http/1.1", out, outlen, in, inlen) == true)
		{
			return SSL_TLSEXT_ERR_OK;
		}

		return SSL_TLSEXT_ERR_NOACK;
	}

	bool TlsContext::SelectALPNProtocol(ov::String key, const unsigned char **out, unsigned char *outlen, const unsigned char *in, unsigned int inlen)
	{
		unsigned int i = 0;
		while (i < inlen)
		{
			auto length = in[i];
			ov::String protocol(reinterpret_cast<const char *>(&in[i + 1]), length);

			if (protocol == key)
			{
				logtt("Selected ALPN protocol: %s", protocol.CStr());
				*out = &in[i + 1];
				*outlen = length;
				return true;
			}

			i += length + 1;
		}

		return false;
	}

	int TlsContext::OnServerNameCallback(SSL *s, int *ad, void *arg)
	{
		return static_cast<TlsContext *>(arg)->OnServerName(s);
	}

	// https://www.openssl.org/docs/man1.1.1/man3/SSL_CTX_set_tlsext_servername_callback.html
	int TlsContext::OnServerName(SSL *ssl)
	{
		ov::String server_name = ::SSL_get_servername(ssl, TLSEXT_NAMETYPE_host_name);

		if (server_name.IsEmpty() == false)
		{
			// Client set a server name
			bool result = DO_CALLBACK_IF_AVAILABLE(bool, false, this, sni_callback, ssl, server_name);

			if (result == false)
			{
				logtw("Could not select certificate: %s", server_name.CStr());
			}
		}
		else
		{
			logtt("Server name is not specified");
		}

		return SSL_TLSEXT_ERR_OK;
	}

	void TlsContext::SetCertificate(const std::shared_ptr<const ::Certificate> &certificate)
	{
		OV_ASSERT2(certificate != nullptr);

		if (certificate == nullptr)
		{
			throw OpensslError("certificate is nullptr");
		}

		if (::SSL_CTX_use_certificate(_ssl_ctx, certificate->GetCertification()) != 1)
		{
			throw OpensslError("Cannot use certificate: %s", OpensslError().What());
		}

		auto chain_cert_path = certificate->GetChainCertificationPath();

		if (chain_cert_path != nullptr)
		{
			::SSL_CTX_load_verify_file(_ssl_ctx, chain_cert_path);
		}

		if (::SSL_CTX_use_PrivateKey(_ssl_ctx, certificate->GetPrivateKey()) != 1)
		{
			throw OpensslError("Cannot use private key: %s", OpensslError().What());
		}
	}

	bool TlsContext::UseSslContext(SSL *ssl)
	{
		return (::SSL_set_SSL_CTX(ssl, _ssl_ctx) != nullptr);
	}

	void TlsContext::SetVerify(int mode)
	{
		::SSL_CTX_set_verify(_ssl_ctx, mode, nullptr);
	}

	int TlsContext::TlsVerify(X509_STORE_CTX *store, void *arg)
	{
		bool result = DO_CALLBACK_IF_AVAILABLE(bool, false, arg, verify_callback, store);

		return result ? 1 : 0;
	}

}  // namespace ov
