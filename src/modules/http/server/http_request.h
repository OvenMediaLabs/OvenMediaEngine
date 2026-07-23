//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include "../protocol/http1/http_request_parser.h"
#include "http_request_interceptor.h"

namespace http
{
	namespace svr
	{
		class HttpExchange;

		class HttpRequest : public ov::EnableSharedFromThis<HttpRequest>
		{
		public:
			friend class RequestInterceptor;

			HttpRequest(const std::shared_ptr<ov::ClientSocket> &client_socket);
			~HttpRequest() override = default;

			std::shared_ptr<ov::ClientSocket> GetRemote();
			std::shared_ptr<const ov::ClientSocket> GetRemote() const;

			void SetTlsData(const std::shared_ptr<ov::TlsServerData> &tls_data);
			std::shared_ptr<ov::TlsServerData> GetTlsData();

			void SetConnectionType(ConnectionType type);
			ConnectionType GetConnectionType() const;

			// Full URI (including domain and port)
			// Example: http://<domain>:<port>/<app>/<stream>/...
			const ov::String &GetUri() const noexcept
			{
				return _request_uri;
			}

			// Get parsed URI
			const std::shared_ptr<ov::Url> &GetParsedUri() const noexcept
			{
				return _parsed_uri;
			}

			std::shared_ptr<const ov::Data> GetRequestBody() const
			{
				return _request_body;
			}

			void SetMatchResult(ov::MatchResult match_result)
			{
				_match_result = std::move(match_result);
			}

			const ov::MatchResult &GetMatchResult() const
			{
				return _match_result;
			}

			// Received server name using SNI
			ov::String GetServerName() const
			{
				if (_tls_data != nullptr)
				{
					return _tls_data->GetTls().GetServerName();
				}

				return "";
			}

			double GetHttpVersionAsNumber() const noexcept
			{
				return ov::Converter::ToDouble(GetHttpVersion().CStr());
			}

			// Get Create Time
			const std::chrono::system_clock::time_point &GetCreateTime() const noexcept
			{
				return _created_time;
			}

			virtual ov::String ToString() const;

			virtual ssize_t AppendHeaderData(const std::shared_ptr<const ov::Data> &data) = 0;
			virtual StatusCode GetHeaderParingStatus() const = 0;
			virtual Method GetMethod() const noexcept = 0;
			virtual ov::String GetHttpVersion() const noexcept = 0;
			virtual ov::String GetHost() const noexcept = 0;
			virtual ov::String GetRequestTarget() const noexcept = 0;
			virtual ov::String GetHeader(const ov::String &key) const noexcept = 0;
			virtual bool IsHeaderExists(const ov::String &key) const noexcept = 0;

			ov::String GetHeader(const ov::String &key, ov::String default_value) const noexcept
			{
				if (IsHeaderExists(key))
				{
					return GetHeader(key);
				}

				return default_value;
			}


		protected:
			const std::shared_ptr<ov::Data> &GetRequestBodyInternal()
			{
				if (_request_body == nullptr)
				{
					_request_body = std::make_shared<ov::Data>();
				}

				return _request_body;
			}

			void PostHeaderParsedProcess();
			void UpdateUri();

			std::shared_ptr<ov::ClientSocket> _client_socket;
			ConnectionType _connection_type = ConnectionType::Unknown;
			std::shared_ptr<ov::TlsServerData> _tls_data;

			ov::MatchResult _match_result;
			ov::String _request_uri;
			std::shared_ptr<ov::Url> _parsed_uri;

		private:
			// HTTP body
			std::shared_ptr<ov::Data> _request_body;
			// Created time
			std::chrono::system_clock::time_point _created_time;
		};
	}  // namespace svr
}  // namespace http
