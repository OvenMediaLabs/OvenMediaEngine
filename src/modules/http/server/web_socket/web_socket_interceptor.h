//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <modules/http/server/http_server.h>
#include <shared_mutex>
#include "../../protocol/web_socket/web_socket_frame.h"

namespace http
{
	namespace svr
	{
		namespace ws
		{
			class Interceptor : public RequestInterceptor
			{
			public:
				Interceptor();
				~Interceptor() override;

				// ws.on('connection');
				void SetConnectionHandler(ConnectionHandler handler);
				// ws.on('message');
				void SetMessageHandler(MessageHandler handler);
				// ws.on('error');
				void SetErrorHandler(ErrorHandler handler);
				// ws.on('close');
				void SetCloseHandler(CloseHandler handler);

			protected:
				//--------------------------------------------------------------------
				// Implementation of HttpRequestInterceptorInterface
				//--------------------------------------------------------------------
				bool IsInterceptorForRequest(const std::shared_ptr<const HttpExchange> &client) override;

				// If these handler return false, the connection will be disconnected
				bool OnRequestPrepared(const std::shared_ptr<HttpExchange> &exchange) override;
				bool OnDataReceived(const std::shared_ptr<HttpExchange> &exchange, const std::shared_ptr<const ov::Data> &data) override;
				InterceptorResult OnRequestCompleted(const std::shared_ptr<HttpExchange> &exchange) override;
				void OnClosed(const std::shared_ptr<HttpConnection> &stream, PhysicalPortDisconnectReason reason) override;

				ConnectionHandler _connection_handler;
				MessageHandler _message_handler;
				ErrorHandler _error_handler;
				CloseHandler _close_handler;
			};
		}  // namespace ws
	}	   // namespace svr
}  // namespace http
