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
		class HttpClientFileInterceptor : public HttpClientInterceptor
		{
		public:
			using DownloadCallback = std::function<void(
				const std::shared_ptr<const ResponseInfo> &response_info,
				const ov::String &file_path,
				const std::shared_ptr<const ov::Error> &error)>;

		public:
			void SetFilePath(const ov::String &file_path)
			{
				_file_path = file_path;
			}

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
			std::shared_ptr<const ov::Error> _error;

			ov::String _file_path;
			FILE *_file = nullptr;
		};
	}  // namespace clnt
}  // namespace http
