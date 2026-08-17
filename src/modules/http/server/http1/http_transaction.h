//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2022 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include "../http_exchange.h"
#include "../../protocol/http2/http2_frame.h"
#include "http1_request.h"
#include "http1_response.h"

namespace http
{
	namespace svr
	{
		namespace h1
		{
			// For HTTP/2.0
			class HttpTransaction : public HttpExchange
			{
			public:
				// Constructor
				HttpTransaction(const std::shared_ptr<HttpConnection> &connection);

				ssize_t OnRequestPacketReceived(const std::shared_ptr<const ov::Data> &data);

				// Implement HttpExchange
				std::shared_ptr<HttpRequest> GetRequest() const override;
				std::shared_ptr<HttpResponse> GetResponse() const override;

			private:
				std::shared_ptr<Http1Request> _request;
				std::shared_ptr<Http1Response> _response;

				size_t _received_header_size = 0;
				size_t _received_data_size = 0;
			};
		}  // namespace h1
	}  // namespace svr
}  // namespace http