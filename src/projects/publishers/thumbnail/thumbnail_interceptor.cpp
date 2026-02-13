//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Kwon Keuk Han
//  Copyright (c) 2019 AirenSoft. All rights reserved.
//
//==============================================================================

#include "thumbnail_interceptor.h"
#include "thumbnail_private.h"

//====================================================================================================
// IsInterceptorForRequest
//====================================================================================================
bool ThumbnailInterceptor::IsInterceptorForRequest(const std::shared_ptr<const http::svr::HttpExchange> &client)
{
	const auto request = client->GetRequest();
	
	// logtt("Request Target : %s", request->GetRequestTarget().CStr());

	// Get Method 1.1 check
	if(request->GetMethod() != http::Method::Get)
	{
		return false;
	}

	const auto target = request->GetRequestTarget().LowerCaseString();

	if ((target.IndexOf(".jpg") >= 0) ||
		(target.IndexOf(".png") >= 0) ||
		(target.IndexOf(".webp") >= 0))
	{
		return true;
	}

	return false;
}
