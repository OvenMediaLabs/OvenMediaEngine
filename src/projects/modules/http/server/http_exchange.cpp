//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================
#include "http_exchange.h"

#include "./http_server_private.h"
#include "http_connection.h"
#include "http_server_manager.h"

namespace http
{
	namespace svr
	{
		// For HTTP/1.1
		HttpExchange::HttpExchange(const std::shared_ptr<HttpConnection> &connection)
			: _connection(connection)
		{
		}

		HttpExchange::HttpExchange(const std::shared_ptr<HttpExchange> &exchange)
		{
			_status = exchange->_status;
			_connection = exchange->_connection;
			_extra = exchange->_extra;
			_keep_alive = exchange->_keep_alive;
		}

		HttpExchange::~HttpExchange()
		{
		}

		// Terminate
		void HttpExchange::Release()
		{
			// print debug info
			// 200 / 300 are not error
			if ((static_cast<int>(GetResponse()->GetStatusCode()) / 100) != 2 &&
				(static_cast<int>(GetResponse()->GetStatusCode()) / 100) != 3)
			{
				logte("\n%s", GetDebugInfo().CStr());
			}
			else
			{
				logtt("\n%s", GetDebugInfo().CStr());
			}

			_status = Status::Completed;
			_connection->OnExchangeCompleted(GetSharedPtr());
		}

		ov::String HttpExchange::ToString() const
		{
			return GetConnection()->ToString();
		}

		ov::String HttpExchange::GetDebugInfo() const
		{
			auto request = GetRequest();
			auto response = GetResponse();

			// Get duration with reqeust->GetCreateTime and response->GetResponseTime
			auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(response->GetResponseTime() - request->GetCreateTime()).count();

			return ov::String::FormatString(
				"[Client] %s (Elapsed: %" PRId64 ") \n"
				"[Request] %s %s (HTTP/%s, Request Time: %s)\n"
				"[Response] %d %s (%d bytes sent, Response Time: %s)",

				ToString().CStr(), duration,

				http::StringFromMethod(request->GetMethod()).CStr(), request->GetUri().CStr(), request->GetHttpVersion().CStr(), ov::Converter::ToISO8601String(request->GetCreateTime()).CStr(),

				ov::ToUnderlyingType(response->GetStatusCode()), http::StringFromStatusCode(response->GetStatusCode()),
				response->GetSentSize(), ov::Converter::ToISO8601String(response->GetResponseTime()).CStr());
		}

		void HttpExchange::SetKeepAlive(bool keep_alive)
		{
			_keep_alive = keep_alive;
		}

		// Get Status
		HttpExchange::Status HttpExchange::GetStatus() const
		{
			return _status;
		}

		// Set Status
		void HttpExchange::SetStatus(HttpExchange::Status status)
		{
			_status = status;
		}

		// Get Connection
		std::shared_ptr<HttpConnection> HttpExchange::GetConnection() const
		{
			return _connection;
		}

		bool HttpExchange::IsHttp2UpgradeRequest()
		{
			//TODO(h2) : Implement this
			return false;
		}

		bool HttpExchange::IsWebSocketUpgradeRequest()
		{
			// RFC6455 - 4.2.1.  Reading the Client's Opening Handshake
			//
			// 1.   An HTTP/1.1 or higher GET request, including a "Request-URI"
			//      [RFC2616] that should be interpreted as a /resource name/
			//      defined in Section 3 (or an absolute HTTP/HTTPS URI containing
			//      the /resource name/).
			//
			// 2.   A |Host| header field containing the server's authority.
			//

			if ((GetRequest()->GetMethod() == Method::Get) && (GetRequest()->GetHttpVersionAsNumber() > 1.0))
			{
				if (
					// 3.   An |Upgrade| header field containing the value "websocket",
					//      treated as an ASCII case-insensitive value.
					(GetRequest()->GetHeader("UPGRADE").UpperCaseString().IndexOf("WEBSOCKET") >= 0L) &&

					// 4.   A |Connection| header field that includes the token "Upgrade",
					//      treated as an ASCII case-insensitive value.
					(GetRequest()->GetHeader("CONNECTION").UpperCaseString().IndexOf("UPGRADE") >= 0L) &&

					// 5.   A |Sec-WebSocket-Key| header field with a base64-encoded (see
					//      Section 4 of [RFC4648]) value that, when decoded, is 16 bytes in
					//      length.
					GetRequest()->IsHeaderExists("SEC-WEBSOCKET-KEY") &&

					// 6.   A |Sec-WebSocket-Version| header field, with a value of 13.
					(GetRequest()->GetHeader("SEC-WEBSOCKET-VERSION") == "13"))
				{
					// 7.   Optionally, an |Origin| header field.  This header field is sent
					//      by all browser clients.  A connection attempt lacking this
					//      header field SHOULD NOT be interpreted as coming from a browser
					//      client.
					//
					// 8.   Optionally, a |Sec-WebSocket-Protocol| header field, with a list
					//      of values indicating which protocols the client would like to
					//      speak, ordered by preference.
					//
					// 9.   Optionally, a |Sec-WebSocket-Extensions| header field, with a
					//      list of values indicating which extensions the client would like
					//      to speak.  The interpretation of this header field is discussed
					//      in Section 9.1.
					//
					// 10.  Optionally, other header fields, such as those used to send
					//      cookies or request authentication to a server.  Unknown header
					//      fields are ignored, as per [RFC2616].

					logtt("%s is websocket request", GetRequest()->ToString().CStr());

					return true;
				}
			}

			return false;
		}

		bool HttpExchange::AcceptWebSocketUpgrade()
		{
			// RFC6455 - 4.2.2.  Sending the Server's Opening Handshake
			GetResponse()->SetStatusCode(StatusCode::SwitchingProtocols);

			GetResponse()->SetHeader("Upgrade", "websocket");
			GetResponse()->SetHeader("Connection", "Upgrade");

			// 4.  A |Sec-WebSocket-Accept| header field.  The value of this
			//    header field is constructed by concatenating /key/, defined
			//    above in step 4 in Section 4.2.2, with the string "258EAFA5-
			//    E914-47DA-95CA-C5AB0DC85B11", taking the SHA-1 hash of this
			//    concatenated value to obtain a 20-byte value and base64-
			//    encoding (see Section 4 of [RFC4648]) this 20-byte hash.
			const ov::String unique_id = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
			ov::String key = GetRequest()->GetHeader("SEC-WEBSOCKET-KEY");

			std::shared_ptr<ov::Data> hash = ov::MessageDigest::ComputeDigest(ov::CryptoAlgorithm::Sha1, (key + unique_id).ToData(false));
			ov::String base64 = ov::Base64::Encode(hash);

			GetResponse()->SetHeader("Sec-WebSocket-Accept", base64);

			// Send headers to client
			if (GetResponse()->Response() <= 0)
			{
				return false;
			}

			return true;
		}

		// Get Connection Policy
		bool HttpExchange::IsKeepAlive() const
		{
			return _keep_alive;
		}

		void HttpExchange::SetConnectionPolicyByRequest()
		{
			if (GetRequest()->GetHttpVersionAsNumber() == 1.0)
			{
				_keep_alive = false;
			}
			else if (GetRequest()->GetHttpVersionAsNumber() == 1.1 || GetRequest()->GetHttpVersionAsNumber() == 2.0)
			{
				_keep_alive = true;
			}

			if (GetRequest()->GetHeader("Connection") == "Keep-Alive")
			{
				_keep_alive = true;
				GetResponse()->AddHeader("Connection", "Keep-Alive");
				GetResponse()->AddHeader("Keep-Alive", "timeout=5, max=100");
			}
			else if (GetRequest()->GetHeader("Connection") == "Close")
			{
				_keep_alive = false;
				GetResponse()->AddHeader("Connection", "Close");
			}
		}

		void HttpExchange::OnRequestPrepared()
		{
			// Find interceptor using received header
			_interceptor = GetConnection()->FindInterceptor(GetSharedPtr());
			if (_interceptor != nullptr)
			{
				_interceptor->OnRequestPrepared(GetSharedPtr());
			}
		}

		void HttpExchange::OnDataReceived(const std::shared_ptr<const ov::Data> &data)
		{
			if (_interceptor != nullptr)
			{
				_interceptor->OnDataReceived(GetSharedPtr(), data);
			}
		}

		InterceptorResult HttpExchange::OnRequestCompleted()
		{
			if (_interceptor == nullptr)
			{
				logte("Cannot find interceptor for %s", GetRequest()->ToString().CStr());
				GetResponse()->SetStatusCode(StatusCode::NotFound);
				GetResponse()->Response();
				Release();
				return InterceptorResult::NotFound;
			}

			auto request = GetRequest();
			auto response = GetResponse();

			response->SetMethod(request->GetMethod());

			auto if_none_match = request->GetHeader("If-None-Match");
			if (if_none_match.IsEmpty() == false)
			{
				response->SetIfNoneMatch(if_none_match);
			}

			// Setup Default Cors Header, application specific CORS header can be overridden
			http::svr::HttpServerManager::GetInstance()->GetDefaultCorsManager().SetupDefaultHttpCorsHeader(request, response, {http::Method::Options, http::Method::Head, http::Method::Get, http::Method::Post, http::Method::Put, http::Method::Patch, http::Method::Delete});

			response->SetHeader("Vary", "Origin");

			return _interceptor->OnRequestCompleted(GetSharedPtr());
		}

	}  // namespace svr
}  // namespace http
