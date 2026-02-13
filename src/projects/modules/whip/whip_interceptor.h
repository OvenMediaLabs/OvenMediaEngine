//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2023 AirenSoft. All rights reserved.
//
//==============================================================================

#pragma once

#include <modules/http/server/http_default_interceptor.h>

class WhipInterceptor : public http::svr::DefaultInterceptor
{
protected:
	bool IsInterceptorForRequest(const std::shared_ptr<const http::svr::HttpExchange> &exchange) override
	{
		auto request = exchange->GetRequest();
		auto uri = request->GetParsedUri();
		if (uri == nullptr)
		{
			return false;
		}

		if (request->GetMethod() == http::Method::Post ||
			request->GetMethod() == http::Method::Delete || 
			request->GetMethod() == http::Method::Patch || 
			request->GetMethod() == http::Method::Options)
		{
			auto direction = uri->GetQueryValue("direction");
			if (direction == "whip")
			{
				return true;
			}
		}

		return false;
	}

	bool IsCacheable() const override
	{
		return false;
	}
};
