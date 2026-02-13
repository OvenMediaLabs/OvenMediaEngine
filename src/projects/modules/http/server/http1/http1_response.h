//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2022 AirenSoft. All rights reserved.
//
//==============================================================================

#pragma once

#include "../http_response.h"

namespace http
{
	namespace svr
	{
		namespace h1
		{
			class Http1Response : public HttpResponse
			{
			public:
				// Constructor
				Http1Response(const std::shared_ptr<ov::ClientSocket> &client_socket);

				void SetChunkedTransfer();

				bool SendChunkedData(const void *data, size_t length);
				bool SendChunkedData(const std::shared_ptr<const ov::Data> &data);
				bool IsChunkedTransfer() const;

			private:
				int32_t SendHeader() override;
				int32_t SendPayload() override;

				bool _chunked_transfer = false;
			};
		}
	}
}