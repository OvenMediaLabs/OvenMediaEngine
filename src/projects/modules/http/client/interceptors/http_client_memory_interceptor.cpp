//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2025 AirenSoft. All rights reserved.
//
//==============================================================================
#include "./http_client_memory_interceptor.h"

#include "../http_client_private.h"

namespace http
{
	namespace clnt
	{
		std::shared_ptr<const ov::Error> HttpClientMemoryInterceptor::OnResponseInfo(const std::shared_ptr<const ResponseInfo> &response_info)
		{
			_response_info		= response_info;

			auto content_length = response_info->GetContentLength();

			if (content_length.has_value())
			{
				logtt("Allocating %zu bytes for receiving response data", content_length.value());
				_data = std::make_shared<ov::Data>(content_length.value());
			}
			else
			{
				logtt("Unknown content length");
				_data = std::make_shared<ov::Data>();
			}

			return nullptr;
		}

		std::shared_ptr<const ov::Error> HttpClientMemoryInterceptor::OnData(const std::shared_ptr<const ov::Data> &data)
		{
			OV_ASSERT2(_data != nullptr);

			_data->Append(data);

			return nullptr;
		}

		void HttpClientMemoryInterceptor::OnError(const std::shared_ptr<const ov::Error> &error)
		{
			_error = error;
		}

		void HttpClientMemoryInterceptor::OnRequestFinished()
		{
			if (_download_callback != nullptr)
			{
				_download_callback(_response_info, _data, _error);
			}
		}
	}  // namespace clnt
}  // namespace http
