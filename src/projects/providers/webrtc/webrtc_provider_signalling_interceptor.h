//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2021 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include "modules/http/server/web_socket/web_socket_interceptor.h"

class WebRtcProviderSignallingInterceptor : public http::svr::ws::Interceptor
{
public:
    //--------------------------------------------------------------------
	// Implementation of HttpRequestInterceptorInterface
	//--------------------------------------------------------------------
	bool IsInterceptorForRequest(const std::shared_ptr<const http::svr::HttpExchange> &client) override
	{
		if(http::svr::ws::Interceptor::IsInterceptorForRequest(client) == false)
		{
			return false;
		}

		const auto request = client->GetRequest();
		auto uri = request->GetUri();
		auto parsed_url = ov::Url::Parse(uri);
		if(parsed_url == nullptr)
		{
			return false;
		}
		
		auto direction = parsed_url->GetQueryValue("direction");
		if(direction == "send")
		{
			return true;
		}

		return false;
	}

	bool IsCacheable() const override
	{
		return true;
	}
};
