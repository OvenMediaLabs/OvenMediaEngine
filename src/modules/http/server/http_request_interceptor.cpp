//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================
#include "http_request_interceptor.h"
#include "http_request.h"

namespace http
{
	namespace svr
	{
		const std::shared_ptr<ov::Data> &RequestInterceptor::GetRequestBody(const std::shared_ptr<HttpRequest> &request)
		{
			return request->GetRequestBodyInternal();
		}
	}  // namespace svr
}  // namespace http
