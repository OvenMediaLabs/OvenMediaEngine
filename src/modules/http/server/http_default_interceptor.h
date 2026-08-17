//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <regex>

#include "../http_datastructure.h"
#include "http_request_interceptor.h"

namespace http
{
	namespace svr
	{
		enum class NextHandler : char
		{
			// Call the next handler
			Call,
			// Do not call the next handler
			DoNotCall,
			// Do not call the following handlers. 
			// And since control of it has moved to another thread, the response will be sent later.
			DoNotCallAndDoNotResponse
		};

		class HttpServer;
		class HttpExchange;

		using RequestHandler = std::function<NextHandler(const std::shared_ptr<HttpExchange> &exchange)>;
		using CloseHandler = std::function<void(const std::shared_ptr<HttpConnection> &connection, PhysicalPortDisconnectReason reason)>;

		/// Default HTTP processor
		class DefaultInterceptor : public RequestInterceptor
		{
		public:
			DefaultInterceptor() = default;
			DefaultInterceptor(const ov::String &pattern_prefix);

			// Register handler to handle method and pattern
			// pattern consists of ECMAScript regex (http://www.cplusplus.com/reference/regex/ECMAScript)
			bool Register(Method method, const ov::String &pattern, const RequestHandler &handler);

			bool RegisterGet(const ov::String &pattern, const RequestHandler &handler)
			{
				return Register(Method::Get, pattern, handler);
			}

			bool RegisterPost(const ov::String &pattern, const RequestHandler &handler)
			{
				return Register(Method::Post, pattern, handler);
			}

			bool RegisterPatch(const ov::String &pattern, const RequestHandler &handler)
			{
				return Register(Method::Patch, pattern, handler);
			}

			bool RegisterPut(const ov::String &pattern, const RequestHandler &handler)
			{
				return Register(Method::Put, pattern, handler);
			}

			bool RegisterDelete(const ov::String &pattern, const RequestHandler &handler)
			{
				return Register(Method::Delete, pattern, handler);
			}

			bool SetCloseHandler(const CloseHandler &handler)
			{
				_close_handler = handler;
				return true;
			}

			//--------------------------------------------------------------------
			// Implementation of RequestInterceptor
			//--------------------------------------------------------------------
			virtual bool IsInterceptorForRequest(const std::shared_ptr<const HttpExchange> &client) override;
			virtual bool OnRequestPrepared(const std::shared_ptr<HttpExchange> &exchange) override;
			virtual bool OnDataReceived(const std::shared_ptr<HttpExchange> &exchange, const std::shared_ptr<const ov::Data> &data) override;
			virtual InterceptorResult OnRequestCompleted(const std::shared_ptr<HttpExchange> &exchange) override;
			virtual void OnClosed(const std::shared_ptr<HttpConnection> &stream, PhysicalPortDisconnectReason reason) override;
			virtual bool IsCacheable() const override;
			
		protected:
			struct RequestInfo
			{
#if DEBUG
				ov::String pattern_string;
#endif	// DEBUG
				ov::Regex pattern;
				Method method;
				RequestHandler handler;
			};

			ov::String _pattern_prefix;
			std::vector<RequestInfo> _request_handler_list;
			CloseHandler _close_handler = nullptr;
		};
	}  // namespace svr
}  // namespace http
