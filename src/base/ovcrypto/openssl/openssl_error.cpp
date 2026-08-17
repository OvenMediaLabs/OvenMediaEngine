//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2021 AirenSoft. All rights reserved.
//
//==============================================================================
#include "openssl_error.h"

#include <openssl/err.h>

namespace ov
{
	OpensslError::OpensslError()
		: Error(OPENSSL_ERROR_DOMAIN)
	{
		unsigned long error_code = ::ERR_get_error();
		char buffer[1024];

		::ERR_error_string_n(error_code, buffer, OV_COUNTOF(buffer));

		SetCodeAndMessage(error_code, buffer);
	}

	OpensslError::OpensslError(ov::String message)
		: Error(OPENSSL_ERROR_DOMAIN, std::move(message))
	{
	}

	OpensslError::OpensslError(SSL *ssl, int result)
		: OpensslError()
	{
		auto ssl_error = ::SSL_get_error(ssl, result);

		char buffer[1024];

		::ERR_error_string_n(ssl_error, buffer, OV_COUNTOF(buffer));

		SetCodeAndMessage(ssl_error, buffer);
	}
}  // namespace ov
