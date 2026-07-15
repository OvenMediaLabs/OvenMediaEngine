//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2025 AirenSoft. All rights reserved.
//
//==============================================================================
#include "./http_client_file_interceptor.h"

#include "../http_client_private.h"

namespace http
{
	namespace clnt
	{
		std::shared_ptr<const ov::Error> HttpClientFileInterceptor::OnResponseInfo(const std::shared_ptr<const ResponseInfo> &response_info)
		{
			_response_info = response_info;

			_file		   = ::fopen(_file_path.CStr(), "wb");

			if (_file == nullptr)
			{
				auto error = ov::Error::CreateErrorFromErrno();

				logte("Failed to open file '%s': %s", _file_path.CStr(), error->GetMessage().CStr());

				return ov::Error::CreateError("HttpClientFile", "Failed to open file for writing: %s", error->GetMessage().CStr());
			}

			return nullptr;
		}

		std::shared_ptr<const ov::Error> HttpClientFileInterceptor::OnData(const std::shared_ptr<const ov::Data> &data)
		{
			OV_ASSERT2(_file != nullptr);

			if (_file == nullptr)
			{
				return ov::Error::CreateError("HttpClientFile", "File is not opened for writing");
			}

			auto buffer	   = data->GetDataAs<uint8_t>();
			auto remaining = data->GetLength();

			while (remaining > 0)
			{
				// Write data to file
				size_t written = ::fwrite(buffer, 1, remaining, _file);

				if (written == 0)
				{
					auto error = ov::Error::CreateErrorFromErrno();
					return ov::Error::CreateError("HttpClientFile", "Failed to write data to file: %s", error->GetMessage().CStr());
				}

				remaining -= written;
				buffer += written;
			}

			return nullptr;
		}

		void HttpClientFileInterceptor::OnError(const std::shared_ptr<const ov::Error> &error)
		{
			_error = error;
		}

		void HttpClientFileInterceptor::OnRequestFinished()
		{
			OV_SAFE_FUNC(_file, nullptr, ::fclose, );

			if (_download_callback != nullptr)
			{
				_download_callback(_response_info, _file_path, _error);
			}
		}
	}  // namespace clnt
}  // namespace http
