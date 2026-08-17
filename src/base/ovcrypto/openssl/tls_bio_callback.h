//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <base/ovlibrary/ovlibrary.h>
#include <openssl/crypto.h>
#include <openssl/dtls1.h>
#include <openssl/err.h>
#include <openssl/rand.h>
#include <openssl/ssl.h>
#include <openssl/tls1.h>
#include <openssl/x509v3.h>

#include <stdint.h>
#include <functional>

#include "tls_context.h"

namespace ov
{
	class Tls;

	struct TlsBioCallback
	{
		// Return value: read bytes
		std::function<ssize_t(Tls *tls, void *buffer, size_t length)> read_callback = nullptr;

		// Return value: written bytes
		std::function<ssize_t(Tls *tls, const void *data, size_t length)> write_callback = nullptr;

		std::function<bool(Tls *tls)> destroy_callback = nullptr;

		std::function<long(Tls *tls, int cmd, long num, void *ptr)> ctrl_callback = nullptr;
	};
}  // namespace ov
