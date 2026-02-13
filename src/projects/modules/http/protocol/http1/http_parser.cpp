//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2021 AirenSoft. All rights reserved.
//
//==============================================================================
#include "http_parser.h"

#include "../http_protocol_private.h"

namespace http
{
	namespace prot
	{
		namespace h1
		{
			ssize_t HttpParser::AppendData(const std::shared_ptr<const ov::Data> &data)
			{
				if (_is_header_found)
				{
					// Once the header is found, it should not be called here (since the parse status is already OK)
					OV_ASSERT2(false);
					return 0L;
				}

				const char new_lines[] = "\r\n\r\n";
				// Exclude null character
				const int new_lines_length = OV_COUNTOF(new_lines) - 1;

				ssize_t used_length = data->GetLength();

				// Since the header has not been parsed yet, try to parse the header every time data comes in
				ssize_t previous_length = _header_string.GetLength();

				// ov::String is binary-safe
				_header_string.Append(data->GetDataAs<char>(), data->GetLength());
				ssize_t newline_position = _header_string.IndexOf(&(new_lines[0]));

				if (newline_position >= 0)
				{
					// If the parser find "\r\n\r\n", start parsing
					_header_string.SetLength(newline_position);

					// Used length =
					//             [Length of string after finding "\r\n\r\n"] -
					//             [Length of string before finding] +
					//             [Length of string "\r\n\r\n"]
					used_length = _header_string.GetLength() - previous_length + new_lines_length;

					_is_header_found = true;
					_parse_status = ParseMessage();

					if (_parse_status != StatusCode::OK)
					{
						// An error occurred during parsing
						used_length = -1L;
						_parse_status = StatusCode::BadRequest;
					}
				}
				else
				{
					// Need more data

					// Check if data consists of non-binary data
					auto data_to_check = data->GetDataAs<char>();
					auto remained = data->GetLength();

					while (remained > 0)
					{
						char character = *data_to_check;

						// isprint(): 32 <= character <= 126
						// isspace(): 9 <= character <= 13
						// reference: https://en.cppreference.com/w/cpp/string/byte/isprint
						if (::isprint(character) || ::isspace(character))
						{
							character++;
							remained--;

							continue;
						}
						else
						{
							// Binary data found
							used_length = -1;
							_parse_status = StatusCode::BadRequest;
							break;
						}
					}
				}

				return used_length;
			}

			StatusCode HttpParser::ParseMessage()
			{
				// RFC7230 - 3. Message Format
				// HTTP-message   = start-line
				//                  *( header-field CRLF )
				//                  CRLF
				//                  [ message-body ]

				// RFC7230 - 3.1. Start Line
				// start-line     = request-line / status-line

				// Tokenize by "\r\n"
				std::vector<ov::String> tokens = _header_string.Split("\r\n");

				StatusCode status_code;

				status_code = ParseFirstLine(tokens[0]);
				tokens.erase(tokens.begin());

				if (status_code == StatusCode::OK)
				{
					for (const ov::String &token : tokens)
					{
						status_code = ParseHeader(token);

						if (status_code != StatusCode::OK)
						{
							break;
						}
					}
				}

				logtt("Headers: %ld:", _headers.size());

				std::for_each(_headers.begin(), _headers.end(), [](const auto &pair) -> void {
					logtt("\t>> %s: %s", pair.first.CStr(), pair.second.CStr());
				});

				_has_content_length = IsHeaderExists("CONTENT-LENGTH");
				auto content_length = ov::Converter::ToInt64(GetHeader("CONTENT-LENGTH", "0"));

				if (content_length < 0)
				{
					logtw("Invalid Content-Length: %s", GetHeader("CONTENT-LENGTH").value_or("N/A").CStr());
					content_length = 0;
				}

				_content_length = static_cast<size_t>(content_length);

				return status_code;
			}

			StatusCode HttpParser::ParseHeader(const ov::String &line)
			{
				// RFC7230 - 3.2.  Header Fields
				// header-field   = field-name ":" OWS field-value OWS
				//
				// field-name     = token
				// field-value    = *( field-content / obs-fold )
				// field-content  = field-vchar [ 1*( SP / HTAB ) field-vchar ]
				// field-vchar    = VCHAR / obs-text
				//
				// obs-fold       = CRLF 1*( SP / HTAB )
				//                ; obsolete line folding
				//                ; see Section 3.2.4

				// Do not handle OBS-FOLD for the following reasons:
				//
				// RFC7230 - 3.2.4.  Field Parsing
				// Historically, HTTP header field values could be extended over
				// multiple lines by preceding each extra line with at least one space
				// or horizontal tab (obs-fold).  This specification deprecates such
				// line folding except within the message/http media type
				// (Section 8.3.1).  A sender MUST NOT generate a message that includes
				// line folding (i.e., that has any field-value that contains a match to
				// the obs-fold rule) unless the message is intended for packaging
				// within the message/http media type.

				ssize_t colon_index = line.IndexOf(':');

				if (colon_index == -1)
				{
					logtw("Invalid header (could not find colon): %s", line.CStr());
					return StatusCode::BadRequest;
				}

				ov::String field_name = line.Left(static_cast<size_t>(colon_index));
				// Eliminate OWS(optional white space) to simplify processing
				ov::String field_value = line.Substring(colon_index + 1).Trim();

				_headers[field_name.Trim()] = field_value.Trim();

				return StatusCode::OK;
			}
		}  // namespace h1
	}	   // namespace prot
}  // namespace http
