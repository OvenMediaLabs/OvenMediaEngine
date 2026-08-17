//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2021 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <base/ovlibrary/ovlibrary.h>
#include <base/ovsocket/client_socket.h>

#include "http1/http_transaction.h"
#include "http2/http2_stream.h"
#include "web_socket/web_socket_session.h"

#include "../protocol/http2/http2_preface.h"
#include "../hpack/encoder.h"
#include "../hpack/decoder.h"

//TODO(Getroot) : Move to Server.xml
#define HTTP_CONNECTION_TIMEOUT_MS		60 * 1000
#define WEBSOCKET_CONNECTION_TIMEOUT_MS	WEBSOCKET_PING_INTERVAL_MS * 3

namespace http
{
	namespace svr
	{
		class HttpServer;
		class HttpsServer;

		class HttpConnection : public ov::EnableSharedFromThis<HttpConnection>
		{
		public:
			HttpConnection(const std::shared_ptr<HttpServer> &server, const std::shared_ptr<ov::ClientSocket> &client_socket);
			
			void OnDataReceived(const std::shared_ptr<const ov::Data> &data);
			void Close(PhysicalPortDisconnectReason reason);

			void OnExchangeCompleted(const std::shared_ptr<HttpExchange> &exchange);

			bool OnRepeatTask();

			void SetTlsData(const std::shared_ptr<ov::TlsServerData> &tls_data);
			void OnTlsAccepted();

			std::shared_ptr<ov::TlsServerData> GetTlsData() const;
			std::shared_ptr<ov::ClientSocket> GetSocket() const;
			ConnectionType GetConnectionType() const;
			std::shared_ptr<HttpExchange> GetHttpExchange() const;
			// Get Websocket Session
			std::shared_ptr<ws::WebSocketSession> GetWebSocketSession() const;

			// Get ID
			uint32_t GetId() const;

			// Get interceptor, return cached interceptor
			std::shared_ptr<RequestInterceptor> GetInterceptor() const;
			// Find interceptor
			std::shared_ptr<RequestInterceptor> FindInterceptor(const std::shared_ptr<HttpExchange> &exchange);

			// Get HPACK Codec
			std::shared_ptr<hpack::Encoder> GetHpackEncoder() const;
			std::shared_ptr<hpack::Decoder> GetHpackDecoder() const;

			// To string
			virtual ov::String ToString() const;

			// User data
			void SetUserData(std::any user_data);
			std::any GetUserData() const;

			void AddUserData(const ov::String &key, std::any user_data);
			const std::any *GetUserData(const ov::String &key) const;
			// Get user datas
			const std::map<ov::String, std::any>& GetUserDataMap() const;

		private:
			// For HTTP 1.0 and HTTP 1.1
			ssize_t OnHttp1RequestReceived(const std::shared_ptr<const ov::Data> &data);
			ssize_t OnHttp2RequestReceived(const std::shared_ptr<const ov::Data> &data);
			ssize_t OnWebSocketDataReceived(const std::shared_ptr<const ov::Data> &data);

			bool AddStream(const std::shared_ptr<HttpExchange> &exchange);
			std::shared_ptr<HttpExchange> FindHttpStream(uint32_t stream_id);
			bool DeleteHttpStream(uint32_t stream_id);

			bool UpgradeToWebSocket(const std::shared_ptr<HttpExchange> &exchange);

			void InitializeHttp2Connection();

			void CheckTimeout();

			// User Data
			std::any _user_data;
			std::map<ov::String, std::any> _user_data_map;
			
			// Default : HTTP 1.1
			ConnectionType _connection_type = ConnectionType::Http11;
			std::shared_ptr<HttpServer> _server = nullptr;
			std::shared_ptr<ov::ClientSocket> _client_socket = nullptr;
			std::shared_ptr<ov::TlsServerData> _tls_data;

			///////////////////////
			// For HTTP/1.1
			///////////////////////
			std::shared_ptr<h1::HttpTransaction> _http_transaction = nullptr;

			///////////////////////
			// For HTTP/2.0
			///////////////////////

			// HTTP/2 Preface
			Http2Preface _http2_preface;
			// HTTP/2 Frame
			std::shared_ptr<Http2Frame> _http2_frame = nullptr;
			// Stream Identifier, HttpExchange
			std::map<uint32_t, std::shared_ptr<h2::HttpStream>> _http_stream_map;
			// mutex
			std::mutex _http_stream_map_guard;
			// HTTP/2 HPACK Codec
			std::shared_ptr<hpack::Encoder> _hpack_encoder = nullptr;
			std::shared_ptr<hpack::Decoder> _hpack_decoder = nullptr;

			///////////////////////
			// For Websocket
			///////////////////////
			std::shared_ptr<ws::WebSocketSession> _websocket_session = nullptr;
			// Websocket Frame
			std::shared_ptr<prot::ws::Frame> _websocket_frame = nullptr;

			std::shared_ptr<RequestInterceptor> _interceptor = nullptr;
			std::vector<std::shared_ptr<RequestInterceptor>> _need_to_close_interceptors;

			std::recursive_mutex _close_mutex;
			bool _closed = false;
		};
	} // namespace svr
} // namespace http