//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================
#include "./web_socket_interceptor.h"

#include <base/ovcrypto/ovcrypto.h>
#include <modules/http/server/http_server.h>

#include <utility>

#include "web_socket_datastructure.h"
#include "../../protocol/web_socket/web_socket_frame.h"
#include "web_socket_private.h"

#define WEBSOCKET_ERROR_DOMAIN "WebSocket"

namespace http
{
	namespace svr
	{
		namespace ws
		{
			Interceptor::Interceptor()
			{
			}

			Interceptor::~Interceptor()
			{
			}

			bool Interceptor::IsInterceptorForRequest(const std::shared_ptr<const HttpExchange> &client)
			{
				const auto request = client->GetRequest();

				if (request->GetConnectionType() != ConnectionType::WebSocket)
				{
					logtt("%s is not websocket request", request->ToString().CStr());
					return false;
				}

				return true;
			}

			bool Interceptor::OnRequestPrepared(const std::shared_ptr<HttpExchange> &exchange)
			{
				auto websocket_session = std::dynamic_pointer_cast<WebSocketSession>(exchange);
				if (websocket_session == nullptr)
				{
					logtw("%s is not websocket session", exchange->GetConnection()->ToString().CStr());
					return false;
				}

				if (_connection_handler != nullptr)
				{
					return _connection_handler(websocket_session);
				}
				else
				{
					logte("WebSocket interceptor has not connection_handler");
					return false;
				}

				return true;
			}

			bool Interceptor::OnDataReceived(const std::shared_ptr<HttpExchange> &exchange, const std::shared_ptr<const ov::Data> &data)
			{
				auto websocket_session = std::dynamic_pointer_cast<WebSocketSession>(exchange);
				if (websocket_session == nullptr)
				{
					logtw("%s is not websocket session", exchange->GetConnection()->ToString().CStr());
					return false;
				}

				// Callback the payload
				if (_message_handler != nullptr)
				{
					if (_message_handler(websocket_session, data) == false)
					{
						return -1;
					}
				}

				return data->GetLength();
			}

			InterceptorResult Interceptor::OnRequestCompleted(const std::shared_ptr<HttpExchange> &exchange)
			{
				return InterceptorResult::Completed;
			}

			void Interceptor::OnClosed(const std::shared_ptr<HttpConnection> &connection, PhysicalPortDisconnectReason reason)
			{
				auto websocket_session = connection->GetWebSocketSession();
				if (websocket_session == nullptr)
				{
					logtt("%s is already closed", connection->ToString().CStr());
					return;
				}

				if (_close_handler != nullptr)
				{
					_close_handler(websocket_session, reason);
				}
			}

			void Interceptor::SetConnectionHandler(ConnectionHandler handler)
			{
				_connection_handler = std::move(handler);
			}

			void Interceptor::SetMessageHandler(MessageHandler handler)
			{
				_message_handler = std::move(handler);
			}

			void Interceptor::SetErrorHandler(ErrorHandler handler)
			{
				_error_handler = std::move(handler);
			}

			void Interceptor::SetCloseHandler(CloseHandler handler)
			{
				_close_handler = std::move(handler);
			}
		}  // namespace ws
	}	   // namespace svr
}  // namespace http
