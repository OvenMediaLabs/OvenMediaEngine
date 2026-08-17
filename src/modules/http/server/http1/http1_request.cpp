//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2022 AirenSoft. All rights reserved.
//
//==============================================================================
#include "http1_request.h"
#include "../http_server_private.h"

namespace http
{
	namespace svr
	{
		namespace h1
		{
// Constructor
			Http1Request::Http1Request(const std::shared_ptr<ov::ClientSocket> &client_socket)
				: HttpRequest(client_socket)
			{
			}

			size_t Http1Request::GetContentLength() const noexcept
			{
				return _http_header_parser.GetContentLength();
			}

			/////////////////////////////////////
			// Implementation of HttpRequest
			/////////////////////////////////////

			ssize_t Http1Request::AppendHeaderData(const std::shared_ptr<const ov::Data> &data)
			{
				if (GetHeaderParingStatus() == StatusCode::OK)
				{
					// Already parsed
					return 0;
				}
				else if (GetHeaderParingStatus() == StatusCode::PartialContent)
				{
					auto consumed_bytes = _http_header_parser.AppendData(data);
					if (GetHeaderParingStatus() == StatusCode::OK)
					{
						PostHeaderParsedProcess();
					}
					return consumed_bytes;
				}
				else
				{
					// Error
					return -1;
				}
			}

			StatusCode Http1Request::GetHeaderParingStatus() const
			{
				return _http_header_parser.GetStatus();
			}

			Method Http1Request::GetMethod() const noexcept
			{
				return _http_header_parser.GetMethod();
			}

			ov::String Http1Request::GetHttpVersion() const noexcept
			{
				return _http_header_parser.GetHttpVersion();
			}

			ov::String Http1Request::GetHost() const noexcept
			{
				return GetHeader("Host");
			}

			ov::String Http1Request::GetRequestTarget() const noexcept
			{
				return _http_header_parser.GetRequestTarget();
			}

			ov::String Http1Request::GetHeader(const ov::String &key) const noexcept
			{
				return _http_header_parser.GetHeader(key).value_or("");
			}

			bool Http1Request::IsHeaderExists(const ov::String &key) const noexcept
			{
				return _http_header_parser.IsHeaderExists(key);
			}

			ov::String Http1Request::ToString() const
			{
				auto result = HttpRequest::ToString();

				auto headers = _http_header_parser.GetHeaders();
				result.AppendFormat("\n[Headers] (%zu):\n", headers.size());
				for (auto &header : headers)
				{
					result.AppendFormat("%s: %s\n", header.first.CStr(), header.second.CStr());
				}

				return result;
			}
		} // namespace h1
	} // namespace svr
} // namespace http