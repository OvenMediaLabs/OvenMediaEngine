//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2021 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <base/ovlibrary/ovlibrary.h>

#include "http_datastructure.h"

#define HTTP_ERROR_DOMAIN "HTTP"

namespace http
{
	class HttpError : public ov::Error
	{
	public:
		HttpError(StatusCode status_code, const char *message)
			: ov::Error(HTTP_ERROR_DOMAIN, static_cast<int>(status_code), message)
		{
		}

		template <typename... Args>
		HttpError(StatusCode status_code, const char *format, Args... args)
			: ov::Error(HTTP_ERROR_DOMAIN, static_cast<int>(status_code), ov::String::FormatString(format, args...))
		{
		}

		static std::shared_ptr<HttpError> CreateError(StatusCode status_code, const char *message)
		{
			return std::make_shared<HttpError>(status_code, message);
		}

		template <typename... Args>
		static std::shared_ptr<HttpError> CreateError(StatusCode status_code, const char *format, Args... args)
		{
			return std::make_shared<HttpError>(status_code, format, args...);
		}

		StatusCode GetStatusCode() const
		{
			return static_cast<StatusCode>(GetCode());
		}
	};
}  // namespace http
