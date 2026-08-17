//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

namespace ov
{
	class TlsContext;

	class TlsContextCallback
	{
	public:
		// Setting up TLS extensions, etc
		std::function<bool(TlsContext *tls, SSL_CTX *context)> create_callback = nullptr;

		// Return value: verified: true, not verified: false
		std::function<bool(TlsContext *tls, X509_STORE_CTX *store_context)> verify_callback = nullptr;

		// for SNI
		std::function<bool(TlsContext *tls, SSL *ssl, const ov::String &server_name)> sni_callback = nullptr;
	};
}  // namespace ov
