//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2022 AirenSoft. All rights reserved.
//
//==============================================================================
#include "web_socket_session.h"

#include "../http_connection.h"

#define OV_LOG_TAG "WebSocket"

namespace http
{
	using namespace prot::ws;

	namespace svr
	{
		namespace ws
		{
			// For Upgrade
			WebSocketSession::WebSocketSession(const std::shared_ptr<HttpExchange> &exchange)
				: HttpExchange(exchange)
			{
				ov::String str("OvenMediaEngine");
				_ping_data = str.ToData(false);

				_request = exchange->GetRequest();
				_request->SetConnectionType(ConnectionType::WebSocket);

				_ws_response = std::make_shared<WebSocketResponse>(exchange->GetResponse());
			}

			void WebSocketSession::AddUserData(ov::String key, std::variant<bool, uint64_t, ov::String> value)
			{
				_data_map.emplace(key, value);
			}

			std::tuple<bool, std::variant<bool, uint64_t, ov::String>> WebSocketSession::GetUserData(ov::String key)
			{
				if (_data_map.find(key) == _data_map.end())
				{
					return {false, false};
				}

				return {true, _data_map[key]};
			}


			// Go Upgrade
			bool WebSocketSession::Upgrade()
			{
				// Find Interceptor
				auto interceptor = GetConnection()->FindInterceptor(GetSharedPtr());
				if (interceptor == nullptr)
				{
					SetStatus(Status::Error);
					GetResponse()->SetStatusCode(StatusCode::NotFound);
					GetResponse()->Response();
					return false;
				}

				interceptor->OnRequestPrepared(GetSharedPtr());

				SetStatus(HttpExchange::Status::Exchanging);

				_ping_timer.Start();

				return true;
			}

			std::shared_ptr<WebSocketResponse> WebSocketSession::GetWebSocketResponse() const
			{
				return _ws_response;
			}

			// Implement HttpExchange
			std::shared_ptr<HttpRequest> WebSocketSession::GetRequest() const
			{
				return _request;
			}
			std::shared_ptr<HttpResponse> WebSocketSession::GetResponse() const
			{
				return _ws_response;
			}

			bool WebSocketSession::Ping()
			{
				if (_ping_timer.IsElapsed(WEBSOCKET_PING_INTERVAL_MS) == false)
				{
					// If the timer is not expired, do not send ping
					return true;
				}

				_ping_timer.Update();

				return _ws_response->Send(_ping_data, FrameOpcode::Ping) > 0;
			}

			bool WebSocketSession::OnFrameReceived(const std::shared_ptr<const prot::ws::Frame> &frame)
			{
				auto interceptor = GetConnection()->FindInterceptor(GetSharedPtr());
				if (interceptor == nullptr)
				{
					SetStatus(Status::Error);
					return false;
				}
				const std::shared_ptr<const ov::Data> payload = frame->GetPayload();
				const auto &header = frame->GetHeader();

				switch (static_cast<FrameOpcode>(header.opcode))
				{
					case FrameOpcode::ConnectionClose:
						// The client requested close the connection
						logtt("Client requested close connection: reason:\n%s", payload->Dump("Reason").CStr());
						interceptor->OnRequestCompleted(GetSharedPtr());
						SetStatus(HttpExchange::Status::Completed);
						return true;

					case FrameOpcode::Ping:
						logtt("A ping frame is received:\n%s", payload->Dump().CStr());
						// Send a pong frame to the client
						_ws_response->Send(payload, FrameOpcode::Pong);
						break;

					case FrameOpcode::Pong:
						// Ignore pong frame
						logtt("A pong frame is received:\n%s", payload->Dump().CStr());
						break;

					case FrameOpcode::Text:
					case FrameOpcode::Binary:
						logtt("%s:\n%s", frame->ToString().CStr(), payload->Dump("Frame", 0L, 1024L, nullptr).CStr());

						if (_fragmented_message != nullptr)
						{
							logtw("A new message started before the fragmented message was completed: %s", ToString().CStr());
							SetStatus(Status::Error);
							return false;
						}

						if (header.fin)
						{
							return OnMessageReceived(interceptor, payload);
						}

						_fragmented_message = payload->Clone();
						break;

					case FrameOpcode::Continuation:
						logtt("%s:\n%s", frame->ToString().CStr(), payload->Dump("Frame", 0L, 1024L, nullptr).CStr());

						if (_fragmented_message == nullptr)
						{
							logtw("A continuation frame was received without a fragmented message: %s", ToString().CStr());
							SetStatus(Status::Error);
							return false;
						}

						_fragmented_message->Append(payload);

						if (header.fin)
						{
							return OnMessageReceived(interceptor, std::exchange(_fragmented_message, nullptr));
						}
						break;

					default:
						logtw("An unknown opcode %d was received: %s", header.opcode, ToString().CStr());
						SetStatus(Status::Error);
						return false;
				}

				return true;
			}

			bool WebSocketSession::OnMessageReceived(const std::shared_ptr<RequestInterceptor> &interceptor, const std::shared_ptr<const ov::Data> &message)
			{
				if (interceptor->OnDataReceived(GetSharedPtr(), message) == false)
				{
					SetStatus(Status::Error);
					return false;
				}

				return true;
			}
		}  // namespace ws
	}	   // namespace svr
}  // namespace http