//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================
#include "http_request.h"

#include <algorithm>

#include "./http_server_private.h"

namespace http
{
	namespace svr
	{
		HttpRequest::HttpRequest(const std::shared_ptr<ov::ClientSocket> &client_socket)
			: _client_socket(client_socket)
		{
			OV_ASSERT2(client_socket != nullptr);
			_created_time = std::chrono::system_clock::now();
		}

		void HttpRequest::SetTlsData(const std::shared_ptr<ov::TlsServerData> &tls_data)
		{
			_tls_data = tls_data;
		}

		std::shared_ptr<ov::TlsServerData> HttpRequest::GetTlsData()
		{
			return _tls_data;
		}

		std::shared_ptr<ov::ClientSocket> HttpRequest::GetRemote()
		{
			return _client_socket;
		}

		std::shared_ptr<const ov::ClientSocket> HttpRequest::GetRemote() const
		{
			return _client_socket;
		}

		void HttpRequest::SetConnectionType(ConnectionType type)
		{
			_connection_type = type;
			UpdateUri();
		}

		ConnectionType HttpRequest::GetConnectionType() const
		{
			return _connection_type;
		}

		void HttpRequest::PostHeaderParsedProcess()
		{
			UpdateUri();
		}

		void HttpRequest::UpdateUri()
		{
			auto host = GetHost();
			if (host.IsEmpty())
			{
				host = _client_socket->GetLocalAddress()->GetIpAddress();
			}

			ov::String scheme;
			if (GetConnectionType() == ConnectionType::Http10 || 
				GetConnectionType() == ConnectionType::Http11 || 
				GetConnectionType() == ConnectionType::Http20)
			{
				scheme = "http";
			}
			else if (GetConnectionType() == ConnectionType::WebSocket)
			{
				scheme = "ws";
			}

			_request_uri = ov::String::FormatString("%s%s://%s%s", scheme.CStr(), (_tls_data != nullptr) ? "s" : "", host.CStr(), GetRequestTarget().CStr());
			_parsed_uri = ov::Url::Parse(_request_uri);
		}

		ov::String HttpRequest::ToString() const
		{
			return ov::String::FormatString("<HttpRequest: %p> Method: %s Verion: %s Uri: %s", this, StringFromMethod(GetMethod()).CStr(), GetHttpVersion().CStr(), GetUri().CStr());
		}
	}  // namespace svr
}  // namespace http
