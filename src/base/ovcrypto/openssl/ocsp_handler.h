//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2023 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <map>
#include <mutex>

#include "./ocsp_cache.h"
#include "./ocsp_context.h"

namespace ov
{
	class OcspHandler
	{
	public:
		bool Setup(SSL_CTX *ssl_ctx);

	protected:
		int OnOcspCallbackInternal(SSL *ssl);
		static int OnOcspCallback(SSL *s, void *arg);

	private:
		static OcspCache _ocsp_cache;

		SSL_CTX *_ssl_ctx = nullptr;
	};
}  // namespace ov