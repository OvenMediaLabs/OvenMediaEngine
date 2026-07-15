//==============================================================================
//
//  ReqeustInfo for access control module
//
//  Created by Getroot
//  Copyright (c) 2024 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <base/ovlibrary/ovlibrary.h>
#include <base/info/host.h>
#include <modules/http/server/http_connection.h>

namespace ac
{
	class RequestInfo
	{
	public:
		RequestInfo(const std::shared_ptr<const ov::Url> &requested_url, const std::shared_ptr<const ov::Url> &backend_url, const std::shared_ptr<const ov::Socket> remote_socket, const std::shared_ptr<const http::svr::HttpRequest> http_request)
			: _requested_url(requested_url), _backend_url(backend_url), _remote_socket(remote_socket), _http_request(http_request) {}

		std::shared_ptr<const ov::Url> GetRequestedUrl() const
		{
			return _requested_url;
		}
		std::shared_ptr<const ov::Url> GetBackendUrl() const
		{
			return _backend_url;
		}
		std::shared_ptr<ov::SocketAddress> GetClientAddress() const
		{
			if (_remote_socket == nullptr)
			{
				return nullptr;
			}
			
			return _remote_socket->GetRemoteAddress();
		}
		const ov::String GetUserAgent() const
		{
			if (_http_request == nullptr)
			{
				return _user_agent;
			}

			return _http_request->GetHeader("User-Agent");
		}
		void SetUserAgent(const ov::String &user_agent)
		{
			_user_agent = user_agent;
		}

		std::optional<ov::String> FindRealIP() const
		{
			// Find real ip from http header

			if (_http_request != nullptr)
			{
				// 1. X-REAL-IP
				if (_http_request->IsHeaderExists("X-REAL-IP") == true)
				{
					return _http_request->GetHeader("X-REAL-IP");
				}
				
				// 2. X-FORWARDED-FOR
				if (_http_request->IsHeaderExists("X-FORWARDED-FOR") == true)
				{
					ov::String forwarded_for = _http_request->GetHeader("X-FORWARDED-FOR");
					auto pos = forwarded_for.IndexOf(',');
					if (pos != -1)
					{
						auto first_item = forwarded_for.Left(pos);
						return first_item.Trim();
					}
					else
					{
						return forwarded_for.Trim();
					}
				}
			}

			if (_remote_socket != nullptr)
			{
				// 3. Connected Remote IP
				if (_remote_socket != nullptr)
				{
					return _remote_socket->GetRemoteAddress()->GetIpAddress();
				}
			}

			return std::nullopt;
		}
		
	private:
		const std::shared_ptr<const ov::Url> _requested_url;
		const std::shared_ptr<const ov::Url> _backend_url; // only for close status
		const std::shared_ptr<const ov::Socket> _remote_socket;
		const std::shared_ptr<const http::svr::HttpRequest> _http_request;
		ov::String _user_agent; // only for close status because there is no http request in close status
	};
}