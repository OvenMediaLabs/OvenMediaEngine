//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <mutex>

#include "http_request.h"
#include "http_response.h"

namespace http
{
	namespace svr
	{
		class HttpConnection;

		class HttpExchange : public ov::EnableSharedFromThis<HttpExchange>
		{
		public:
			enum class Status : uint8_t
			{
				None,
				Init,
				Exchanging, // Exchanging request and response
				Moved, // Control transferred to another thread, header parsed
				Completed, // completed the response
				Upgrade,
				Error
			};

			HttpExchange(const std::shared_ptr<HttpConnection> &connection);
			HttpExchange(const std::shared_ptr<HttpExchange> &exchange); // Copy
			~HttpExchange();

			// Get connection
			std::shared_ptr<HttpConnection> GetConnection() const;

			// Terminate
			void Release();
			
			// Get Status
			Status GetStatus() const;

			// Get Connection Policy
			bool IsKeepAlive() const;

			ov::String ToString() const;
			ov::String GetDebugInfo() const;

			virtual std::shared_ptr<HttpRequest> GetRequest() const = 0;
			virtual std::shared_ptr<HttpResponse> GetResponse() const = 0;

			template <typename T>
			void SetExtra(std::shared_ptr<T> extra)
			{
				_extra = std::move(extra);
			}

			std::any GetExtra() const
			{
				return _extra;
			}

			template <typename T>
			std::shared_ptr<T> GetExtraAs() const
			{
				try
				{
					return std::any_cast<std::shared_ptr<T>>(_extra);
				}
				catch ([[maybe_unused]] const std::bad_any_cast &e)
				{
				}

				return nullptr;
			}

			bool IsWebSocketUpgradeRequest();
			bool IsHttp2UpgradeRequest();

		protected:
			bool AcceptWebSocketUpgrade();
			void SetConnectionPolicyByRequest();
			void SetStatus(Status status);
			void SetKeepAlive(bool keep_alive);

			void OnRequestPrepared();
			void OnDataReceived(const std::shared_ptr<const ov::Data> &data);
			InterceptorResult OnRequestCompleted();

		private:
			std::shared_ptr<HttpConnection> _connection = nullptr;
			Status _status = Status::None;
			bool _keep_alive = true; // HTTP/1.1 default
			std::any _extra;
			std::shared_ptr<RequestInterceptor> _interceptor = nullptr; // Cached interceptor
		};
	}  // namespace svr
}  // namespace http
