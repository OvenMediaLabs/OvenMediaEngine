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
#include "../../protocol/http2/frames/http2_frames.h"
#include "../../hpack/encoder.h"

#define MAX_HTTP2_HEADER_SIZE (1024 * 1024)
#define MAX_HTTP2_DATA_SIZE (16384)

namespace http
{
	namespace svr
	{
		namespace h2
		{
			class Http2Response : public HttpResponse
			{
			public:
				// Constructor
				Http2Response(uint32_t stream_id, const std::shared_ptr<ov::ClientSocket> &client_socket, const std::shared_ptr<hpack::Encoder> &hpack_encoder);

				bool Send(const std::shared_ptr<prot::h2::Http2Frame> &frame);

				// After Response(), EndStream flag is not sent.
				void SetKeepStream(bool keep_stream);
				bool Send(const std::shared_ptr<prot::h2::Http2DataFrame> &data_frame, bool end_stream);

			protected:
				using HttpResponse::Send;

			private:
				int32_t SendHeader() override;
				int32_t SendPayload() override;

				uint32_t _stream_id = 0;
				bool _keep_stream = false;
				std::shared_ptr<hpack::Encoder> _hpack_encoder;
			};
		}
	}
}