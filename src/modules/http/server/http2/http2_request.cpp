//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2022 AirenSoft. All rights reserved.
//
//==============================================================================
#include "http2_request.h"

#include "../http_server_private.h"

namespace http
{
	namespace svr
	{
		namespace h2
		{
			// Constructor
			Http2Request::Http2Request(const std::shared_ptr<ov::ClientSocket> &client_socket, const std::shared_ptr<hpack::Decoder> &hpack_decoder)
				: HttpRequest(client_socket)
			{
				_hpack_decoder = hpack_decoder;
			}

			/////////////////////////////////////
			// Implementation of HttpRequest
			/////////////////////////////////////

			ssize_t Http2Request::AppendHeaderData(const std::shared_ptr<const ov::Data> &data)
			{
				if (GetHeaderParingStatus() == StatusCode::OK)
				{
					// Already parsed
					return 0;
				}

				if (_hpack_decoder == nullptr)
				{
					logte("Http2Request::AppendHeaderData() - Http2Request::_hpack_decoder is nullptr");
					_parse_status = StatusCode::InternalServerError;
					return -1;
				}

				std::vector<hpack::HeaderField> header_fields;
				auto result = _hpack_decoder->Decode(data, header_fields);
				if (result == false)
				{
					logte("Failed to decode header data");
					_parse_status = StatusCode::BadRequest;
					return -1;
				}

				for (const auto &header_field : header_fields)
				{
					_headers.emplace(header_field.GetName().Trim(), header_field.GetValue().Trim());
				}

				_parse_status = StatusCode::OK;

				PostHeaderParsedProcess();

				return data->GetLength();
			}

			StatusCode Http2Request::GetHeaderParingStatus() const
			{
				return _parse_status;
			}

			Method Http2Request::GetMethod() const noexcept
			{
				auto it = _headers.find(":method");
				if (it == _headers.end())
				{
					return Method::Unknown;
				}

				return MethodFromString(it->second);
			}

			ov::String Http2Request::GetHttpVersion() const noexcept
			{
				return "2";
			}

			ov::String Http2Request::GetHost() const noexcept
			{
				static const ov::String empty = "";

				auto it = _headers.find(":authority");
				if (it == _headers.end())
				{
					return empty;
				}

				return it->second;
			}

			// Path of the URI (including query strings & excluding domain and port)
			// Example: /<app>/<stream>/...?a=b&c=d
			ov::String Http2Request::GetRequestTarget() const noexcept
			{
				static const ov::String empty = "";

				auto it = _headers.find(":path");
				if (it == _headers.end())
				{
					return empty;
				}

				return it->second;
			}

			ov::String Http2Request::GetHeader(const ov::String &key) const noexcept
			{
				auto it = _headers.find(key);
				if (it == _headers.end())
				{
					return "";
				}

				return it->second;
			}

			bool Http2Request::IsHeaderExists(const ov::String &key) const noexcept
			{
				auto it = _headers.find(key);
				if (it == _headers.end())
				{
					return false;
				}

				return true;
			}

			ov::String Http2Request::ToString() const
			{
				ov::String result; 

				result = HttpRequest::ToString();

				// Output all headers
				result.AppendFormat("\n[Headers] (%zu):\n", _headers.size());
				for (const auto &header : _headers)
				{
					result.AppendFormat("%s: %s\n", header.first.CStr(), header.second.CStr());
				}

				return result;
			}
		}  // namespace h2
	}	   // namespace svr
}  // namespace http