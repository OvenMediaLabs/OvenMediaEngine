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

#include "./ocsp_context.h"

namespace ov
{
	class OcspCache
	{
	public:
		std::shared_ptr<OcspContext> ContextForCert(SSL_CTX *ssl_ctx, X509 *cert);
		~OcspCache();

	protected:
		struct Comparator
		{
			bool operator()(const X509 *lhs, const X509 *rhs) const
			{
				return ::X509_cmp(lhs, rhs) < 0;
			}
		};

	private:
		std::mutex _cache_mutex;
		// We should call X509_free when we erase an item from the map
		std::map<X509 *, std::shared_ptr<OcspContext>, Comparator> _cache_map;
	};
}  // namespace ov