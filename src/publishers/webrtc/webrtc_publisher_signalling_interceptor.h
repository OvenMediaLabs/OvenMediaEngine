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

class WebRtcPublisherSignallingInterceptor : public http::svr::ws::Interceptor
{
public:
	//--------------------------------------------------------------------
	// Implementation of HttpRequestInterceptorInterface
	//--------------------------------------------------------------------
	bool IsInterceptorForRequest(const std::shared_ptr<const http::svr::HttpExchange> &exchange) override
	{
		if (http::svr::ws::Interceptor::IsInterceptorForRequest(exchange) == false)
		{
			return false;
		}

		auto request	= exchange->GetRequest();
		auto uri		= request->GetUri();
		auto parsed_url = ov::Url::Parse(uri);
		if (parsed_url == nullptr)
		{
			return false;
		}

		// Accept if URL has not "direction" query string or has not "direction=send"
		// ws://host:port/app/stream
		// ws://host:port/app/stream?direction=recv
		// ws://host:port/app/stream?direction=1je921
		auto direction = parsed_url->GetQueryValue("direction");
		if (direction.IsEmpty() || direction != "send")
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
