//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2023 AirenSoft. All rights reserved.
//
//==============================================================================
#include "./ocsp_context.h"

#include "./openssl_private.h"

namespace ov
{
	OcspContext::OcspContext(SSL_CTX *ssl_ctx, X509 *cert)
		: _ssl_ctx(ssl_ctx)
	{
		_status_request_enabled = IsStatusRequestEnabled(cert);

		if (_status_request_enabled)
		{
			logtt("OCSP stapling is enabled: %p", cert);

			if (ParseOcspUrl(cert) == false)
			{
				logtw("OCSP stapling is enabled, but there is no responder URL in the certificate. OCSP stapling is disabled: %p", cert);
				_status_request_enabled = false;
			}
		}
		else
		{
			logtt("OCSP stapling is disabled: %p", cert);
		}
	}

	bool OcspContext::FindStatusRequestExtension(X509_EXTENSION *ext)
	{
		static const ov::String status_request = "status_request";

		auto method = ::X509V3_EXT_get(ext);
		if ((method == nullptr) || (method->i2v == nullptr))
		{
			return false;
		}

		auto extoct = ::X509_EXTENSION_get_data(ext);
		auto p = ::ASN1_STRING_get0_data(extoct);
		auto extlen = ::ASN1_STRING_length(extoct);

		RaiiPtr<ASN1_VALUE> value(
			(method->it)
				? ASN1_item_d2i(NULL, &p, extlen, ASN1_ITEM_ptr(method->it))
				: static_cast<ASN1_VALUE *>(method->d2i(NULL, &p, extlen)),
			[method](ASN1_VALUE *instance) {
				if (method->it)
				{
					::ASN1_item_free(instance, ASN1_ITEM_ptr(method->it));
				}
				else
				{
					method->ext_free(instance);
				}
			});
		if (value == nullptr)
		{
			return false;
		}

		RaiiPtr<STACK_OF(CONF_VALUE)> conf_values(
			method->i2v(method, value, nullptr),
			[](STACK_OF(CONF_VALUE) * instance) {
				sk_CONF_VALUE_pop_free(instance, ::X509V3_conf_free);
			});

		if (conf_values == nullptr)
		{
			return false;
		}

		const int count = ::sk_CONF_VALUE_num(conf_values);

		for (int index = 0; index < count; index++)
		{
			const auto conf_value = sk_CONF_VALUE_value(conf_values, index);

			if ((conf_value != nullptr) && (status_request == conf_value->value))
			{
				return true;
			}
		}

		return false;
	}

	bool OcspContext::IsStatusRequestEnabled(const X509 *cert)
	{
		auto extensions = ::X509_get0_extensions(cert);

		const int count = ::sk_X509_EXTENSION_num(extensions);

		if (count <= 0)
		{
			return false;
		}

		for (int index = 0; index < count; index++)
		{
			auto extension_value = sk_X509_EXTENSION_value(extensions, index);
			auto obj = ::X509_EXTENSION_get_object(extension_value);

			if ((OBJ_obj2nid(obj) == NID_tlsfeature) && FindStatusRequestExtension(extension_value))
			{
				return true;
			}
		}

		return false;
	}

	std::shared_ptr<OcspContext> OcspContext::Create(SSL_CTX *ssl_ctx, X509 *cert)
	{
		return std::make_shared<OcspContext>(ssl_ctx, cert);
	}

	// Get the responder URL from certificate
	bool OcspContext::ParseOcspUrl(X509 *cert)
	{
		const auto aia = RaiiPtr<STACK_OF(OPENSSL_STRING)>(::X509_get1_ocsp(cert), ::X509_email_free);

		if (aia != nullptr)
		{
			const auto aia_url = sk_OPENSSL_STRING_value(aia, 0);

			logtt("Trying to parse AIA URL: %s", aia_url);

			int use_tls;
			if (::OSSL_HTTP_parse_url(
					aia_url,
					&use_tls,
					nullptr,
					_host.GetPointer(), _port.GetPointer(), nullptr,
					_path.GetPointer(), nullptr,
					nullptr) != 0)
			{
				_url = aia_url;
				_use_tls = use_tls == 1;

				_ssl_request_ctx.Set(use_tls ? ::SSL_CTX_new(::TLS_client_method()) : nullptr);

				logtt("Parsed: host=%s, port=%s, path=%s, use_tls=%d", _host.Get(), _port.Get(), _path.Get(), _use_tls);

				return true;
			}

			logte("Could not parse AIA URL: %s", aia_url);
		}
		else
		{
			logte("There is no AIA and responder URL in the certificate");
		}

		return false;
	}

	// Create a OCSP query for server certificate
	bool OcspContext::Request(X509 *cert)
	{
		logtt("Trying to query OCSP request to responder: %s", _url.c_str());

		auto store_ctx = RaiiPtr<X509_STORE_CTX>(
			::X509_STORE_CTX_new(),
			::X509_STORE_CTX_free);
		if (store_ctx == nullptr)
		{
			logte("Could not create store context");
			_response_code = SSL_TLSEXT_ERR_NOACK;
			return false;
		}

		if (::X509_STORE_CTX_init(
				store_ctx,
				::SSL_CTX_get_cert_store(_ssl_ctx),
				nullptr, nullptr) == 0)
		{
			logte("Could not initialize store context");
			_response_code = SSL_TLSEXT_ERR_NOACK;
			return false;
		}

		const auto issuer_name = ::X509_get_issuer_name(cert);
		auto obj = RaiiPtr<X509_OBJECT>(
			::X509_STORE_CTX_get_obj_by_subject(store_ctx, X509_LU_X509, issuer_name),
			::X509_OBJECT_free);

		if (obj == nullptr)
		{
			logte("Could not obtain issuer certificate");
			_response_code = SSL_TLSEXT_ERR_NOACK;
			return false;
		}

		auto id = RaiiPtr<OCSP_CERTID>(
			::OCSP_cert_to_id(nullptr, cert, ::X509_OBJECT_get0_X509(obj)),
			::OCSP_CERTID_free);
		if (id == nullptr)
		{
			logte("Could not create OCSP certificate ID");
			_response_code = SSL_TLSEXT_ERR_NOACK;
			return false;
		}

		auto request = RaiiPtr<OCSP_REQUEST>(::OCSP_REQUEST_new(), ::OCSP_REQUEST_free);
		if (request == nullptr)
		{
			logte("Could not create OCSP request");
			_response_code = SSL_TLSEXT_ERR_NOACK;
			return false;
		}

		if (::OCSP_request_add0_id(request, id) == 0)
		{
			logte("Could not add certificate ID to OCSP request");
			_response_code = SSL_TLSEXT_ERR_NOACK;
			return false;
		}

		// id should not be freed here because it is assigned to other values within OCSP_request_add0_id()
		id.Detach();

		// Add extensions if needed
		// if (HandleExtensions(ssl, request) == false)
		// {
		// 	logte("Failed to handle extensions");
		// 	_response_code = SSL_TLSEXT_ERR_ALERT_FATAL;
		// 	return false;
		// }

		auto req_mem = RaiiPtr<BIO>(
			::ASN1_item_i2d_mem_bio(ASN1_ITEM_rptr(OCSP_REQUEST), request.GetAs<ASN1_VALUE>()),
			::BIO_free);
		if (req_mem == nullptr)
		{
			logte("Error creating BIO for OCSP request");
			_response_code = SSL_TLSEXT_ERR_ALERT_FATAL;
			return false;
		}

		auto request_bio = RaiiPtr<BIO>(
			::OSSL_HTTP_transfer(
				nullptr,
				_host, _port, _path, _use_tls ? 1 : 0,
				nullptr, 0, nullptr, nullptr,
				OnHttpTlsResponse, this,
				0, nullptr,
				"application/ocsp-request", req_mem,
				"application/ocsp-response", 1,
				OSSL_HTTP_DEFAULT_MAX_RESP_LEN, -1,
				0),
			::BIO_free);

		_response = static_cast<OCSP_RESPONSE *>(
			::ASN1_item_d2i_bio(
				ASN1_ITEM_rptr(OCSP_RESPONSE),
				request_bio,
				nullptr));

		if (_response == nullptr)
		{
			logte("Error querying OCSP responder");
			_response_code = SSL_TLSEXT_ERR_ALERT_FATAL;
			return false;
		}

		_response_code = SSL_TLSEXT_ERR_OK;
		_received_time = std::chrono::system_clock::now();
		return true;
	}

	bool OcspContext::HasResponse() const
	{
		return (_response != nullptr);
	}

	const RaiiPtr<OCSP_RESPONSE> &OcspContext::GetResponse()
	{
		return _response;
	}

	int OcspContext::GetResponseCode() const
	{
		return _response_code;
	}

	bool OcspContext::HandleExtensions(SSL *ssl, OCSP_REQUEST *request)
	{
		STACK_OF(X509_EXTENSION) *exts = nullptr;
		::SSL_get_tlsext_status_exts(ssl, &exts);

		const auto count = ::sk_X509_EXTENSION_num(exts);

		for (int index = 0; index < count; index++)
		{
			auto ext = sk_X509_EXTENSION_value(exts, index);

			if (::OCSP_REQUEST_add_ext(request, ext, -1) == 0)
			{
				logte("Failed to add extension to OCSP request (ext #%d)", index);
				return false;
			}
		}

		return true;
	}

	// Called by OSSL_HTTP_transfer()
	BIO *OcspContext::OnHttpTlsResponse(BIO *bio, int connect, int detail)
	{
		if (_use_tls == false)
		{
			// Nothing to do - Do not use HTTPS for OCSP request
			logtt("OCSP responder is not HTTPS");
			return bio;
		}

		if (connect)
		{
			auto sbio = RaiiPtr<BIO>(::BIO_new(::BIO_f_ssl()), ::BIO_free);
			if (sbio == nullptr)
			{
				logte("Could not allocate BIO");
				return nullptr;
			}

			auto ssl = ::SSL_new(_ssl_request_ctx);
			if (ssl == nullptr)
			{
				logte("Could not allocate SSL");
				return nullptr;
			}

			const auto store = ::SSL_CTX_get_cert_store(_ssl_request_ctx);
			const auto verify_param = ::X509_STORE_get0_param(store);
			if (verify_param != nullptr)
			{
				// idx == 0 indicates first hostname
				::SSL_set_tlsext_host_name(ssl, ::X509_VERIFY_PARAM_get0_host(verify_param, 0));
			}

			::SSL_set_connect_state(ssl);
			::BIO_set_ssl(sbio, ssl, BIO_CLOSE);

			bio = ::BIO_push(sbio.Detach(), bio);
		}
		else
		{
			auto err = ::ERR_peek_error();

			if (::ERR_GET_LIB(err) != ERR_LIB_SSL)
			{
				err = ::ERR_peek_last_error();
			}

			if (::ERR_GET_LIB(err) == ERR_LIB_SSL)
			{
				switch (::ERR_GET_REASON(err))
				{
					case SSL_R_WRONG_VERSION_NUMBER:
						logte("The server does not support (a suitable version of) TLS");
						break;
					case SSL_R_UNKNOWN_PROTOCOL:
						logte("The server does not support HTTPS");
						break;
					case SSL_R_CERTIFICATE_VERIFY_FAILED:
						logte("Cannot authenticate server via its TLS certificate, likely due to mismatch with our trusted TLS certs or missing revocation status");
						break;
					case SSL_AD_REASON_OFFSET + TLS1_AD_UNKNOWN_CA:
						logte("Server did not accept our TLS certificate, likely due to mismatch with server's trust anchor or missing revocation status");
						break;
					case SSL_AD_REASON_OFFSET + SSL3_AD_HANDSHAKE_FAILURE:
						logte("TLS handshake failure. Possibly the server requires our TLS certificate but did not receive it");
						break;
					default:
						break;
				}
			}

			// Suppress errors temporarily
			::ERR_set_mark();
			::BIO_ssl_shutdown(bio);
			auto connect_bio = ::BIO_pop(bio);
			::BIO_free(bio);
			::ERR_pop_to_mark();

			bio = connect_bio;
		}

		return bio;
	}
}  // namespace ov