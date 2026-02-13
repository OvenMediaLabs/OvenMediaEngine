//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2021 AirenSoft. All rights reserved.
//
//==============================================================================
#include "http_response_parser.h"

#include "../http_protocol_private.h"

namespace http
{
	namespace prot
	{
		namespace h1
		{
			StatusCode HttpResponseParser::ParseFirstLine(const ov::String &line)
			{
				// RFC7230 - 3.1.2. Status Line
				// request-line   = method SP request-target SP HTTP-version CRLF

				// status-line = HTTP-version SP status-code SP reason-phrase CRLF
				ssize_t first_space_index = line.IndexOf(' ');
				ssize_t second_space_index = line.IndexOf(' ', first_space_index + 1);

				if ((first_space_index < 0) || (second_space_index < 0) || (first_space_index == second_space_index))
				{
					logtw("Invalid space index: first: %d, last: %d, line: %s", first_space_index, second_space_index, line.CStr());
					return StatusCode::BadRequest;
				}

				// RFC7230 - 2.6. Protocol Versioning
				// HTTP-version  = HTTP-name "/" DIGIT "." DIGIT
				// HTTP-name     = %x48.54.54.50 ; "HTTP", case-sensitive
				_http_version = line.Left(static_cast<size_t>(first_space_index));
				auto tokens = _http_version.Split("/");
				if ((tokens.size() != 2) || (tokens[0] != "HTTP"))
				{
					return StatusCode::BadRequest;
				}

				// RFC7230 - 3.1.2. Status Line
				// status-code   = 3DIGIT
				auto status_code = line.Substring(first_space_index + 1, static_cast<size_t>(second_space_index - first_space_index - 1));
				if (status_code.GetLength() != 3)
				{
					return StatusCode::BadRequest;
				}
				_status_code = static_cast<StatusCode>(ov::Converter::ToInt32(status_code));

				if (IsValidStatusCode(_status_code) == false)
				{
					logtw("Unknown status code: %d", _status_code);
				}

				// RFC7230 - 3.1.2. Status Line
				// reason-phrase  = *( HTAB / SP / VCHAR / obs-text )
				_reason_phrase = line.Substring(second_space_index + 1);

				logtt("Version: [%s], status code: [\"%s\" (%d)], reason: [%s]", _http_version.CStr(), status_code.CStr(), _status_code, _reason_phrase.CStr());

				return StatusCode::OK;
			}
		}  // namespace h1
	} // namespace prot
}  // namespace http
