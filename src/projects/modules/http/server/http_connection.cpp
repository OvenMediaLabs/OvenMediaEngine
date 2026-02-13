#include <base/ovcrypto/openssl/tls_server_data.h>

#include "http_server.h"
#include "http_connection.h"

#include "./http_server_private.h"

namespace http
{
	namespace svr
	{
		HttpConnection::HttpConnection(const std::shared_ptr<HttpServer> &server, const std::shared_ptr<ov::ClientSocket> &client_socket)
		{
			_server = server;
			_client_socket = client_socket;
			_connection_type = ConnectionType::Http11; // Default to HTTP/1.1
		}

		// Get Id from socket
		uint32_t HttpConnection::GetId() const
		{
			return _client_socket->GetNativeHandle();
		}

		void HttpConnection::SetUserData(std::any user_data)
		{
			_user_data = user_data;
		}
		
		std::any HttpConnection::GetUserData() const
		{
			return _user_data;
		}

		void HttpConnection::AddUserData(const ov::String &key, std::any user_data)
		{
			_user_data_map[key] = std::move(user_data);
		}

		const std::any *HttpConnection::GetUserData(const ov::String &key) const
		{
			auto item = _user_data_map.find(key);

			if (item == _user_data_map.end())
			{
				return nullptr;
			}

			return &item->second;
		}

		// Get user datas
		const std::map<ov::String, std::any>& HttpConnection::GetUserDataMap() const
		{
			return _user_data_map;
		}

		// To string
		ov::String HttpConnection::ToString() const
		{
			// Print connection type, client address, client port, use of tls
			return ov::String::FormatString("HttpConnection(%p) : %s %s TLS(%s)",
				this,
				StringFromConnectionType(_connection_type).CStr(),
				_client_socket->ToString().CStr(),
				_tls_data ? "Enabled" : "Disabled");
		}
		
		// Called every 5 seconds
		bool HttpConnection::OnRepeatTask()
		{
			if (_connection_type == ConnectionType::WebSocket)
			{
				if (_websocket_session != nullptr)
				{
					_websocket_session->Ping();
				}
			}

			CheckTimeout();

			return true;
		}

		void HttpConnection::CheckTimeout()
		{
			// Check timeout 
			auto current = std::chrono::high_resolution_clock::now();
			auto elapsed_time_from_last_sent = std::chrono::duration_cast<std::chrono::milliseconds>(current - _client_socket->GetLastSentTime()).count();
			auto elapsed_time_from_last_recv = std::chrono::duration_cast<std::chrono::milliseconds>(current - _client_socket->GetLastRecvTime()).count();

			switch (_connection_type)
			{
				case ConnectionType::Http10:
				case ConnectionType::Http11:
				case ConnectionType::Http20:
					if (std::min(elapsed_time_from_last_recv, elapsed_time_from_last_sent) > HTTP_CONNECTION_TIMEOUT_MS)
					{
						// Close connection
						logti("Client(%s - %s) has timed out", StringFromConnectionType(_connection_type).CStr(), _client_socket->ToString().CStr());
						Close(PhysicalPortDisconnectReason::Disconnect);
					}
					break;
				case ConnectionType::WebSocket:
					// In websocket, if Pong does not arrive for a certain period of time, timeout should be processed. (It takes a very long time to check disconnected by ping transmission because it can be mistaken for sending a ping to a dead client.)
					if (elapsed_time_from_last_recv > WEBSOCKET_CONNECTION_TIMEOUT_MS)
					{
						// Close connection
						logti("Client(%s - %s) has timed out", StringFromConnectionType(_connection_type).CStr(),_client_socket->ToString().CStr());
						Close(PhysicalPortDisconnectReason::Disconnect);
					}
				default:
					return;
			}
		}

		void HttpConnection::OnExchangeCompleted(const std::shared_ptr<HttpExchange> &exchange)
		{
			switch (_connection_type)
			{
				case ConnectionType::Http20:
				{
					auto http2_stream = std::static_pointer_cast<h2::HttpStream>(exchange);

					// Lock
					std::unique_lock<std::mutex> lock(_http_stream_map_guard);
					_http_stream_map.erase(http2_stream->GetStreamId());
					break;
				}
				case ConnectionType::Http10:
				case ConnectionType::Http11:
				case ConnectionType::WebSocket:
				default:
					// Nothing to do
					break;
			}
		}

		// Called from HttpsServer
		void HttpConnection::SetTlsData(const std::shared_ptr<ov::TlsServerData> &tls_data)
		{
			_tls_data = tls_data;
		}

		void HttpConnection::OnTlsAccepted()
		{
			logti("TLS connection accepted : Server Name(%s) Alpn Protocol(%s) Client (%s)", 
					_tls_data->GetServerName().CStr(), _tls_data->GetSelectedAlpnProtocolStr().CStr(), _client_socket->ToString().CStr());

			if (_tls_data->GetSelectedAlpnProtocol() == ov::TlsServerData::AlpnProtocol::Http20)
			{
				_connection_type = ConnectionType::Http20;
			}
			else 
			{
				_connection_type = ConnectionType::Http11;
			}
		}

		// Get Last Exchange
		std::shared_ptr<HttpExchange> HttpConnection::GetHttpExchange() const
		{
			return _http_transaction;
		}

		std::shared_ptr<ws::WebSocketSession> HttpConnection::GetWebSocketSession() const
		{
			return _websocket_session;
		}
		
		// Get Interceptor
		std::shared_ptr<RequestInterceptor> HttpConnection::GetInterceptor() const
		{
			return _interceptor;
		}

		std::shared_ptr<ov::TlsServerData> HttpConnection::GetTlsData() const
		{
			return _tls_data;
		}

		std::shared_ptr<ov::ClientSocket> HttpConnection::GetSocket() const 
		{
			return _client_socket;
		}

		ConnectionType HttpConnection::GetConnectionType() const
		{
			return _connection_type;
		}

		std::shared_ptr<hpack::Encoder> HttpConnection::GetHpackEncoder() const
		{
			return _hpack_encoder;
		}

		std::shared_ptr<hpack::Decoder> HttpConnection::GetHpackDecoder() const
		{
			return _hpack_decoder;
		}

		// Find Interceptor
		std::shared_ptr<RequestInterceptor> HttpConnection::FindInterceptor(const std::shared_ptr<HttpExchange> &exchange)
		{
			std::shared_ptr<RequestInterceptor> interceptor = _interceptor;

			if (interceptor != nullptr)
			{
				return interceptor;
			}
			else 
			{
				// Cache interceptor
				interceptor = _server->FindInterceptor(exchange);
				if (interceptor != nullptr && interceptor->IsCacheable())
				{
					_interceptor = interceptor;
				}

				// If there is no interceptor in _need_to_close_interceptors, add it
				if (interceptor != nullptr)
				{
					auto found = std::find(_need_to_close_interceptors.begin(), _need_to_close_interceptors.end(), interceptor);
					if (found == _need_to_close_interceptors.end())
					{
						_need_to_close_interceptors.push_back(interceptor);
					}
				}
			}

			// Find interceptor from server
			return interceptor;
		}

		bool HttpConnection::UpgradeToWebSocket(const std::shared_ptr<HttpExchange> &exchange)
		{
			_connection_type = ConnectionType::WebSocket;
					
			_websocket_session = std::make_shared<ws::WebSocketSession>(_http_transaction);
			_websocket_session->Upgrade();

			return true;
		}

		void HttpConnection::Close(PhysicalPortDisconnectReason reason)
		{
			// mutex
			std::lock_guard<std::recursive_mutex> lock(_close_mutex);
			if (_closed == true)
			{
				return;
			}

			for (auto &interceptor : _need_to_close_interceptors)
			{
				interceptor->OnClosed(GetSharedPtr(), reason);
			}

			if (_http_transaction != nullptr)
			{
				_http_transaction.reset();
			}

			if (_websocket_frame != nullptr)
			{
				_websocket_frame.reset();
			}

			if (_websocket_session != nullptr)
			{
				_websocket_session.reset();
			}
			
			std::unique_lock<std::mutex> map_guard(_http_stream_map_guard);
			_http_stream_map.clear();
			map_guard.unlock();

			if (reason != PhysicalPortDisconnectReason::Disconnected)
			{
				_client_socket->Close();
			}

			_user_data_map.clear();

			_closed = true;
		}

		void HttpConnection::OnDataReceived(const std::shared_ptr<const ov::Data> &data)
		{
			std::lock_guard<std::recursive_mutex> lock(_close_mutex);
			if (_closed == true)
			{
				return;
			}

			auto process_data = data->Clone();
			while (process_data->GetLength() > 0)
			{
				ssize_t processed_length = 0;

				switch (_connection_type)
				{
					case ConnectionType::Http10:
					case ConnectionType::Http11:
						processed_length = OnHttp1RequestReceived(process_data);
						break;
					case ConnectionType::Http20:
						processed_length = OnHttp2RequestReceived(process_data);
						break;
					case ConnectionType::WebSocket:
						processed_length = OnWebSocketDataReceived(process_data);
						break;
					
					default:
						OV_ASSERT(false, "Unknown connection type");
						return;
				}
				
				// Error or Completed
				if (processed_length <= 0)
				{
					return;
				}

				process_data = process_data->Subdata(processed_length);
			}
		}

		ssize_t HttpConnection::OnHttp1RequestReceived(const std::shared_ptr<const ov::Data> &data)
		{
			// HTTP 1.1 only has one stream
			// Although HTTP/1.1 pipelining allows a client to send multiple requests at once, the server must respond in order.
			// So the server receives and processes requests one by one.

			if (_http_transaction == nullptr)
			{
				_http_transaction = std::make_shared<h1::HttpTransaction>(GetSharedPtr());
			}

			auto processed_data_length = _http_transaction->OnRequestPacketReceived(data);
			if (processed_data_length <= 0)
			{
				// Error
				Close(PhysicalPortDisconnectReason::Error);
				return -1;
			}

			switch(_http_transaction->GetStatus())
			{
				case HttpExchange::Status::Completed:
					if (_http_transaction->IsKeepAlive() == false)
					{
						Close(PhysicalPortDisconnectReason::Disconnect);
						return 0;
					}
					_http_transaction.reset();
					break;

				case HttpExchange::Status::Upgrade:
					if (_http_transaction->IsWebSocketUpgradeRequest())
					{
						UpgradeToWebSocket(_http_transaction);
					}
					else
					{
						// TODO(h2) : Implement this
						OV_ASSERT2(false);
					}
					_http_transaction.reset();
					break;

				case HttpExchange::Status::Moved:
					_http_transaction.reset();
					break;
					
				case HttpExchange::Status::Error:
					Close(PhysicalPortDisconnectReason::Error);
					return -1;

				case HttpExchange::Status::Init:
				case HttpExchange::Status::Exchanging:
				default:
					break;
			}

			return processed_data_length;
		}

		ssize_t HttpConnection::OnWebSocketDataReceived(const std::shared_ptr<const ov::Data> &data)
		{
			if (_websocket_session == nullptr)
			{
				return -1;
			}

			if (_websocket_frame == nullptr)
			{
				_websocket_frame = std::make_shared<prot::ws::Frame>();
			}
		
			ssize_t read_bytes = 0;
			if (_websocket_frame->Process(data, &read_bytes))
			{
				if (_websocket_session->OnFrameReceived(_websocket_frame) == false)
				{
					Close(PhysicalPortDisconnectReason::Error);
					return -1;
				}

				if (_websocket_session->GetStatus() == HttpExchange::Status::Exchanging)
				{
					// Normal
				}
				else if (_websocket_session->GetStatus() == HttpExchange::Status::Completed)
				{
					Close(PhysicalPortDisconnectReason::Disconnect);
					return -1;
				}
				else if (_websocket_session->GetStatus() == HttpExchange::Status::Error)
				{
					Close(PhysicalPortDisconnectReason::Error);
					return -1;
				}
				else 
				{
					OV_ASSERT2(false);
					Close(PhysicalPortDisconnectReason::Error);
					return -1;
				}

				_websocket_frame.reset();
			}

			return read_bytes;
		}

		ssize_t HttpConnection::OnHttp2RequestReceived(const std::shared_ptr<const ov::Data> &data)
		{
			logtt("Http2RequestReceived : %u", data->GetLength());

			ssize_t consumed_bytes = 0;

			if (_http2_preface.IsConfirmed() == false)
			{
				consumed_bytes = _http2_preface.AppendData(data);
				if (consumed_bytes == -1)
				{
					logte("HTTP/2 preface is not confirmed from %s", _client_socket->ToString().CStr());
					return -1;
				}

				if (_http2_preface.IsConfirmed() == true)
				{
					InitializeHttp2Connection();
				}

				return consumed_bytes;
			}

			if (_http2_frame == nullptr)
			{
				logtt("Create HTTP/2 frame");
				_http2_frame = std::make_shared<Http2Frame>();
			}

			consumed_bytes = _http2_frame->AppendData(data);
			if (consumed_bytes < 0)
			{
				Close(PhysicalPortDisconnectReason::Error);
				return -1;
			}

			if (_http2_frame->GetParsingState() == Http2Frame::ParsingState::Completed)
			{
				logtt("HTTP/2 Frame Received : %s", _http2_frame->ToString().CStr());
				
				// lock
				std::unique_lock<std::mutex> lock(_http_stream_map_guard);
				std::shared_ptr<h2::HttpStream> stream;
				auto stream_it = _http_stream_map.find(_http2_frame->GetStreamId());
				if (stream_it != _http_stream_map.end())
				{
					stream = stream_it->second;
				}
				else
				{
					stream = std::make_shared<h2::HttpStream>(GetSharedPtr(), _http2_frame->GetStreamId());
					_http_stream_map.emplace(_http2_frame->GetStreamId(), stream);
					logtt("%s : Streams [%u]", ToString().CStr(), _http_stream_map.size());
				}
				lock.unlock();

				stream->OnFrameReceived(_http2_frame);

				_http2_frame.reset();
			}

			return consumed_bytes;
		}

		void HttpConnection::InitializeHttp2Connection()
		{
			logtt("Initialize HTTP/2 connection");

			_hpack_encoder = std::make_shared<hpack::Encoder>();
			_hpack_decoder = std::make_shared<hpack::Decoder>();

			// Control Stream (stream id : 0) is always open
			std::unique_lock<std::mutex> lock(_http_stream_map_guard);
			_http_stream_map.emplace(0, std::make_shared<h2::HttpStream>(GetSharedPtr(), 0));
		}
	}  // namespace svr
}  // namespace http