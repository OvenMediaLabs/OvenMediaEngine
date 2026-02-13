//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2022 AirenSoft. All rights reserved.
//
//==============================================================================

#pragma once

#include "../http_request.h"
#include "../../hpack/decoder.h"

namespace http
{
	namespace svr
	{
		namespace h2
		{
			class Http2Request : public HttpRequest
			{
			public:
				// Constructor
				Http2Request(const std::shared_ptr<ov::ClientSocket> &client_socket, const std::shared_ptr<hpack::Decoder> &hpack_decoder);

				

				/////////////////////////////////////
				// Implementation of HttpRequest
				/////////////////////////////////////

				ssize_t AppendHeaderData(const std::shared_ptr<const ov::Data> &data) override;
				StatusCode GetHeaderParingStatus() const override;
				
				Method GetMethod() const noexcept override;
				ov::String GetHttpVersion() const noexcept override;
				ov::String GetHost() const noexcept override;
				ov::String GetRequestTarget() const noexcept override;
				ov::String GetHeader(const ov::String &key) const noexcept override;
				bool IsHeaderExists(const ov::String &key) const noexcept override;

				ov::String ToString() const override;

			private:
				std::shared_ptr<hpack::Decoder> _hpack_decoder;

				// https://www.rfc-editor.org/rfc/rfc7230#section-3.2
				// Each header field consists of a case-insensitive field name followed
				// by a colon (":"), optional leading whitespace, the field value, and
				// optional trailing whitespace.

				// https://www.rfc-editor.org/rfc/rfc7540#section-8.1.2
				// Just as in HTTP/1.x, header field names are strings of ASCII
				// characters that are compared in a case-insensitive fashion.  However,
				// header field names MUST be converted to lowercase prior to their
				// encoding in HTTP/2.

				// So _headers is a map of case insentitive header key and value
				HttpHeaderMap _headers;
				StatusCode _parse_status = StatusCode::PartialContent;
			};
		} // namespace h1
	} // namespace svr
}  // namespace http