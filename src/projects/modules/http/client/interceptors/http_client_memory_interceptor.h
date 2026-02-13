//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2025 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include "./http_client_interceptor.h"

namespace http
{
	namespace clnt
	{
		class HttpClientMemoryInterceptor : public HttpClientInterceptor
		{
		public:
			using DownloadCallback = std::function<void(
				const std::shared_ptr<const ResponseInfo> &response_info,
				const std::shared_ptr<ov::Data> &data,
				const std::shared_ptr<const ov::Error> &error)>;

		public:
			void SetDownloadCallback(DownloadCallback callback)
			{
				_download_callback = std::move(callback);
			}

			DownloadCallback GetDownloadCallback() const
			{
				return _download_callback;
			}

		protected:
			//--------------------------------------------------------------------
			// Overriding of HttpClientInterceptor
			//--------------------------------------------------------------------
			std::shared_ptr<const ov::Error> OnResponseInfo(const std::shared_ptr<const ResponseInfo> &response_info) override;
			std::shared_ptr<const ov::Error> OnData(const std::shared_ptr<const ov::Data> &data) override;
			void OnError(const std::shared_ptr<const ov::Error> &error) override;
			void OnRequestFinished() override;

		private:
			std::shared_ptr<const ResponseInfo> _response_info;

			DownloadCallback _download_callback;

			// Response data
			std::shared_ptr<ov::Data> _data;
			std::shared_ptr<const ov::Error> _error;
		};
	}  // namespace clnt
}  // namespace http
