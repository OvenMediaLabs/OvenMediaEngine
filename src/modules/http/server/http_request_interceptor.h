//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <base/ovlibrary/ovlibrary.h>
#include <modules/physical_port/physical_port_observer.h>

#include <memory>

#include "../http_datastructure.h"

namespace http
{
	namespace svr
	{
		class HttpRequest;
		class HttpConnection;
		class HttpExchange;
		class RequestInterceptor
		{
		public:
			virtual ~RequestInterceptor() {}

			// Returns whether the request is an interceptor capable of processing.
			// If this method returns true, it will only pass to this interceptor when data is received in the future, but not to another interceptor.
			virtual bool IsInterceptorForRequest(const std::shared_ptr<const HttpExchange> &client) = 0;

			/// A callback called to initialize request/response immediately after IsInterceptorForRequest()
			///
			/// @param client An instance that contains informations related to HTTP request/response
			///
			/// @return Whether to disconnect with the client
			virtual bool OnRequestPrepared(const std::shared_ptr<HttpExchange> &exchange) = 0;

			/// A callback called if data received from the client.
			///
			/// @param client An instance that contains informations related to HTTP request/response
			/// @param data Received data from the client
			///
			/// @return return <= 0 -> error, return > 0 -> consumed bytes

			virtual bool OnDataReceived(const std::shared_ptr<HttpExchange> &exchange, const std::shared_ptr<const ov::Data> &data) = 0;

			/// A callback called if the request is completed.
			///
			/// @param client An instance that contains informations related to HTTP request/response
			///
			/// @return Whether to disconnect with the client
			virtual InterceptorResult OnRequestCompleted(const std::shared_ptr<HttpExchange> &exchange) = 0;

			/// A callback called when the client is disconnects.
			///
			/// @param client An instance that contains informations related to HTTP request/response
			/// @param reason Indicates why this callback was called
			///
			/// @remark The response can no longer be sent to the client in this state because the connection is already disconnected.
			/// This callback is guaranteed to be called at all times even if an error occurs.
			virtual void OnClosed(const std::shared_ptr<HttpConnection> &connection, PhysicalPortDisconnectReason reason) = 0;

			/// Is it cacheable?
			virtual bool IsCacheable() const = 0;

		protected:
			static const std::shared_ptr<ov::Data> &GetRequestBody(const std::shared_ptr<HttpRequest> &request);
		};
	}  // namespace svr
}  // namespace http
