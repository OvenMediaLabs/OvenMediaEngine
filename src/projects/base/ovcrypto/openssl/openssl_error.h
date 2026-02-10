//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2021 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <base/ovlibrary/ovlibrary.h>
#include <openssl/ssl.h>

#define OPENSSL_ERROR_DOMAIN "OpenSSL"

namespace ov
{
	class OpensslError : public Error
	{
	public:
		// Create an error instance from OpenSSL error code
		OpensslError();
		OpensslError(ov::String message);

		OpensslError(const char *format)
			: Error(OPENSSL_ERROR_DOMAIN, ov::String(format))
		{
		}

		template <typename... Args>
		OpensslError(const char *format, Args... args)
			: Error(OPENSSL_ERROR_DOMAIN, ov::String::FormatString(format, args...))
		{
		}

		OpensslError(SSL *ssl, int result);
	};
}  // namespace ov
