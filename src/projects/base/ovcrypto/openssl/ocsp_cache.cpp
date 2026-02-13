//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2023 AirenSoft. All rights reserved.
//
//==============================================================================
#include "./ocsp_cache.h"

#include <base/ovlibrary/ovlibrary.h>

#include "./openssl_private.h"

namespace ov
{
	std::shared_ptr<OcspContext> OcspCache::ContextForCert(SSL_CTX *ssl_ctx, X509 *cert)
	{
		std::lock_guard lock_guard(_cache_mutex);

		auto item = _cache_map.find(cert);

		std::shared_ptr<OcspContext> ocsp_context;

		if (item != _cache_map.end())
		{
			ocsp_context = item->second;

			if (ocsp_context->IsExpired())
			{
				// Need to issue new OCSP request
				logti("Trying to renew OCSP response: %p", cert);
				auto x509 = item->first;
				_cache_map.erase(item);
				::X509_free(x509);

				ocsp_context = nullptr;
			}
		}

		if (ocsp_context == nullptr)
		{
			// Create new instance
			ocsp_context = OcspContext::Create(ssl_ctx, cert);

			if (ocsp_context == nullptr)
			{
				return nullptr;
			}

			_cache_map[::X509_dup(cert)] = ocsp_context;
		}

		if (ocsp_context->IsStatusRequestEnabled())
		{
			if (ocsp_context->HasResponse() == false)
			{
				if (ocsp_context->Request(cert) == false)
				{
					return nullptr;
				}
			}
			else
			{
				logtt("Use cached OCSP response: %p", cert);
			}
		}

		return ocsp_context;
	}

	OcspCache::~OcspCache()
	{
		std::lock_guard lock_guard(_cache_mutex);

		for (auto item : _cache_map)
		{
			::X509_free(item.first);
		}
	}
}  // namespace ov