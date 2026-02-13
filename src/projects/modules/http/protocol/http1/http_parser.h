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

#include <map>

#include "../../http_datastructure.h"

namespace http
{
	namespace prot
	{
		namespace h1
		{
			class HttpParser
			{
			public:
				/// Process data sent by peers
				///
				/// @param data Received data
				///
				/// @return Size of data used for HTTP parsing. Returns -1L if an error occurs during parsing
				ssize_t AppendData(const std::shared_ptr<const ov::Data> &data);

				/// Parsing status (Updated by ProcessData())
				///
				/// @return HttpStatusCode::PartialContent - Need more data
				///         HttpStatusCode::OK - All data parsed successfully
				///         Other - An error occurred
				StatusCode GetStatus() const
				{
					return _parse_status;
				}

				bool IsParsing() const
				{
					return (_parse_status == StatusCode::PartialContent);
				}

				const HttpHeaderMap &GetHeaders() const noexcept
				{
					return _headers;
				}

				Method GetMethod() const noexcept
				{
					return _method;
				}

				ov::String GetHttpVersion() const noexcept
				{
					auto tokens = _http_version.Split("/");
					if (tokens.size() != 2)
					{
						return "1.1";
					}

					return tokens[1];
				}

				double GetHttpVersionAsNumber() const noexcept
				{
					auto tokens = _http_version.Split("/");

					if (tokens.size() != 2)
					{
						return 0.0;
					}

					return ov::Converter::ToDouble(tokens[1]);
				}

				std::optional<ov::String> GetHeader(const ov::String &key) const noexcept
				{
					auto item = _headers.find(key);

					if (item == _headers.cend())
					{
						return std::nullopt;
					}

					return item->second;
				}

				ov::String GetHeader(const ov::String &key, ov::String default_value) const noexcept
				{
					return GetHeader(key).value_or(default_value);
				}

				const bool IsHeaderExists(const ov::String &key) const noexcept
				{
					return _headers.find(key.LowerCaseString()) != _headers.cend();
				}

				bool HasContentLength() const
				{
					return _has_content_length;
				}

				size_t GetContentLength() const
				{
					return _content_length;
				}

			protected:
				StatusCode ParseMessage();
				virtual StatusCode ParseFirstLine(const ov::String &line) = 0;
				StatusCode ParseHeader(const ov::String &line);

				StatusCode _parse_status = StatusCode::PartialContent;

				Method _method			 = Method::Unknown;
				ov::String _http_version;

				bool _is_header_found = false;
				// A temporary buffer to extract HTTP header
				ov::String _header_string;
				HttpHeaderMap _headers;

				// Frequently used headers
				size_t _content_length	 = 0L;
				bool _has_content_length = false;
			};
		}  // namespace h1
	}  // namespace prot
}  // namespace http
