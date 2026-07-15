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
};
