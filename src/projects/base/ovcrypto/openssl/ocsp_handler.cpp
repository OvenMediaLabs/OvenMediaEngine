//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2023 AirenSoft. All rights reserved.
//
//==============================================================================
#include "./ocsp_handler.h"

#include <base/ovlibrary/ovlibrary.h>
#include <openssl/ssl.h>
#include <openssl/tls1.h>

#include "./ocsp_context.h"
#include "./openssl_private.h"

namespace ov
{
	bool OcspHandler::Setup(SSL_CTX *ssl_ctx)
	{
		if (ssl_ctx == nullptr)
		{
			logtw("SSL context is nullptr");
			return false;
		}

		if (_ssl_ctx != nullptr)
		{
			logte("OCSP Handler is already set up");
			return false;
		}

		_ssl_ctx = ssl_ctx;

		// Set OCSP callback function
		::SSL_CTX_set_tlsext_status_cb(ssl_ctx, &OcspHandler::OnOcspCallback);
		::SSL_CTX_set_tlsext_status_arg(ssl_ctx, this);

		// Prepare OCSP request for the first certificate
		// _ocsp_cache.ContextForCert(_ssl_ctx, ::SSL_CTX_get0_certificate(ssl_ctx));

		return true;
	}

	int OcspHandler::OnOcspCallbackInternal(SSL *ssl)
	{
		RaiiPtr<OCSP_RESPONSE> ocsp_response(nullptr, ::OCSP_RESPONSE_free);

		auto cert = ::SSL_get_certificate(ssl);
		auto ocsp_context = _ocsp_cache.ContextForCert(_ssl_ctx, cert);

		if (ocsp_context != nullptr)
		{
			if (ocsp_context->IsStatusRequestEnabled())
			{
				if (ocsp_context->HasResponse())
				{
					unsigned char *responder = nullptr;
					auto responder_length = ::i2d_OCSP_RESPONSE(ocsp_context->GetResponse(), &responder);

					if (responder_length > 0)
					{
						::SSL_set_tlsext_status_ocsp_resp(ssl, responder, responder_length);

						return SSL_TLSEXT_ERR_OK;
					}
				}

				return ocsp_context->GetResponseCode();
			}
		}

		return SSL_TLSEXT_ERR_OK;
	}

	int OcspHandler::OnOcspCallback(SSL *s, void *arg)
	{
		return static_cast<OcspHandler *>(arg)->OnOcspCallbackInternal(s);
	}

	OcspCache OcspHandler::_ocsp_cache;
};	// namespace ov