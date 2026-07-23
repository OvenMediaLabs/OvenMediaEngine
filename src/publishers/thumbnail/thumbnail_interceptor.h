//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Keukhan
//  Copyright (c) 2019 AirenSoft. All rights reserved.
//
//==============================================================================

#pragma once

#include <modules/http/server/http_server.h>

class ThumbnailInterceptor : public http::svr::DefaultInterceptor
{
public:
    ThumbnailInterceptor()
	{
	}

	~ThumbnailInterceptor() = default;

protected:

	//--------------------------------------------------------------------
	// Implementation of HttpRequestInterceptorInterface
	//--------------------------------------------------------------------
	bool IsInterceptorForRequest(const std::shared_ptr<const http::svr::HttpExchange> &client) override;

	// If cached, this interceptor is pinned to the connection and requests for
	// other publishers sharing the port (e.g. LLHLS over HTTP/2) would be
	// routed here and get 404. So the interceptor must be selected per request.
	bool IsCacheable() const override
	{
		return false;
	}
};
