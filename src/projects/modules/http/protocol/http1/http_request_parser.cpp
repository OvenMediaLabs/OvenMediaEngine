//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2021 AirenSoft. All rights reserved.
//
//==============================================================================
#include "http_request_parser.h"

#include "../http_protocol_private.h"

namespace http
{
	namespace prot
	{
		namespace h1
		{
			StatusCode HttpRequestHeaderParser::ParseFirstLine(const ov::String &line)
			{
				// RFC7230 - 3.1.1. Request Line
				// request-line   = method SP request-target SP HTTP-version CRLF
				ssize_t first_space_index = line.IndexOf(' ');
				ssize_t last_space_index = line.IndexOfRev(' ');

				if ((first_space_index < 0) || (last_space_index < 0) || (first_space_index == last_space_index))
				{
					logtw("Invalid space index: first: %d, last: %d, line: %s", first_space_index, last_space_index, line.CStr());
					return StatusCode::BadRequest;
				}

				// RFC7231 - 4. Request Methods
				ov::String method = line.Left(static_cast<size_t>(first_space_index));

				_method = MethodFromString(method);

				if (_method == Method::Unknown)
				{
					logtw("Unknown method: %s", method.CStr());
					return StatusCode::MethodNotAllowed;
				}

				// RFC7230 - 5.3. Request Target
				// request-target = origin-form
				//            / absolute-form
				//            / authority-form
				//            / asterisk-form
				_request_target = line.Substring(first_space_index + 1, static_cast<size_t>(last_space_index - first_space_index - 1));

				// RFC7230 - 2.6. Protocol Versioning
				// HTTP-version  = HTTP-name "/" DIGIT "." DIGIT
				// HTTP-name     = %x48.54.54.50 ; "HTTP", case-sensitive
				_http_version = line.Substring(last_space_index + 1);

				auto tokens = _http_version.Split("/");
				if ((tokens.size() != 2) || (tokens[0] != "HTTP"))
				{
					logtw("Invalid HTTP version: %s", _http_version.CStr());
					return StatusCode::BadRequest;
				}

				logtt("Method: [%s], uri: [%s], version: [%s]", method.CStr(), _request_target.CStr(), _http_version.CStr());
				return StatusCode::OK;
			}
		}  // namespace h1
	} // namespace prot
}  // namespace http
