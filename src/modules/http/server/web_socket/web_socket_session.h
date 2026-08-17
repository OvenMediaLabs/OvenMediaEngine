//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2022 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include "../../protocol/web_socket/web_socket_frame.h"
#include "../http_exchange.h"
#include "web_socket_response.h"

#define WEBSOCKET_PING_INTERVAL_MS 20 * 1000

namespace http
{
	namespace svr
	{
		namespace ws
		{
			// For WebSocket
			class WebSocketSession : public HttpExchange
			{
			public:
				friend class HttpConnection;

				// Only can be created by upgrade
				WebSocketSession(const std::shared_ptr<HttpConnection> &connection) = delete;
				WebSocketSession(const std::shared_ptr<HttpExchange> &exchange);
				virtual ~WebSocketSession() = default;

				// Go Upgrade
				bool Upgrade();

				// Ping
				bool Ping();

				bool OnFrameReceived(const std::shared_ptr<const prot::ws::Frame> &frame);

				std::shared_ptr<WebSocketResponse> GetWebSocketResponse() const;
								
				// Implement HttpExchange
				std::shared_ptr<HttpRequest> GetRequest() const override;
				std::shared_ptr<HttpResponse> GetResponse() const override;

				// For User Data
				void AddUserData(ov::String key, std::variant<bool, uint64_t, ov::String> value);
				std::tuple<bool, std::variant<bool, uint64_t, ov::String>> GetUserData(ov::String key);
				template <typename T>
				std::optional<T> GetUserDataAs(ov::String key)
				{
					auto [exist, value] = GetUserData(key);
					if ((exist == false) || (std::holds_alternative<T>(value) == false))
					{
						return std::nullopt;
					}

					return std::get<T>(value);
				}

			private:
				// key : data<int or string>
				std::map<ov::String, std::variant<bool, uint64_t, ov::String>> _data_map;
				
				std::shared_ptr<const ov::Data> _ping_data;
				ov::StopWatch _ping_timer;

				std::shared_ptr<HttpRequest> _request;
				std::shared_ptr<WebSocketResponse> _ws_response;
			};
		}  // namespace ws
	} // namespace svr
}  // namespace http