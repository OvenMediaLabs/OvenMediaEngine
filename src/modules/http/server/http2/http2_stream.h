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
#include "../../protocol/http2/frames/http2_frames.h"
#include "http2_request.h"
#include "http2_response.h"

#define MAX_HEADER_TABLE_SIZE 65536u
namespace http
{
	using namespace prot::h2;
	namespace svr
	{
		namespace h2
		{
			// For HTTP/2.0
			class HttpStream : public HttpExchange
			{
			public:
				friend class HttpConnection;

				HttpStream(const std::shared_ptr<HttpConnection> &connection, uint32_t stream_id);
				virtual ~HttpStream() = default;

				// Implement HttpExchange
				std::shared_ptr<HttpRequest> GetRequest() const override;
				std::shared_ptr<HttpResponse> GetResponse() const override;

				// Get Stream ID
				uint32_t GetStreamId() const;
				bool OnFrameReceived(const std::shared_ptr<Http2Frame> &frame);

			private:
				// Send Settings frame and Window_Update frame
				bool SendInitialControlMessage();

				bool OnEndHeaders();
				bool OnEndStream();
				
				// Data frame received
				bool OnDataFrameReceived(const std::shared_ptr<const Http2DataFrame> &frame);
				// Headers frame received
				bool OnHeadersFrameReceived(const std::shared_ptr<const Http2HeadersFrame> &frame);
				// Priority frame received
				bool OnPriorityFrameReceived(const std::shared_ptr<const Http2PriorityFrame> &frame);
				// RST_STREAM frame received
				bool OnRstStreamFrameReceived(const std::shared_ptr<const Http2RstStreamFrame> &frame);
				// Settings frame received
				bool OnSettingsFrameReceived(const std::shared_ptr<const Http2SettingsFrame> &frame);
				// Push Promise frame received
				bool OnPushPromiseFrameReceived(const std::shared_ptr<const Http2PushPromiseFrame> &frame);
				// Ping frame received
				bool OnPingFrameReceived(const std::shared_ptr<const Http2PingFrame> &frame);
				// GoAway frame received
				bool OnGoAwayFrameReceived(const std::shared_ptr<const Http2GoAwayFrame> &frame);
				// WindowUpdate frame received
				bool OnWindowUpdateFrameReceived(const std::shared_ptr<const Http2WindowUpdateFrame> &frame);
				// Continuation frame received
				bool OnContinuationFrameReceived(const std::shared_ptr<const Http2ContinuationFrame> &frame);
				uint32_t _stream_id = 0;

				std::shared_ptr<const Http2HeadersFrame> _headers_frame;
				std::shared_ptr<ov::Data> _header_block = nullptr;
				std::shared_ptr<Http2Request> _request = nullptr;
				std::shared_ptr<Http2Response> _response = nullptr;
			};
		}  // namespace h2
	} // namespace svr
}  // namespace http