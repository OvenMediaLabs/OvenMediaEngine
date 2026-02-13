//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2020 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <modules/physical_port/physical_port.h>

#include <shared_mutex>

#include "../http_error.h"
#include "http_connection.h"
#include "http_default_interceptor.h"

#define HTTP_SERVER_USE_DEFAULT_COUNT PHYSICAL_PORT_USE_DEFAULT_COUNT

// References
//
// RFC7230 - Hypertext Transfer Protocol (HTTP/1.1): Message Syntax and Routing (https://tools.ietf.org/html/rfc7230)
// RFC7231 - Hypertext Transfer Protocol (HTTP/1.1): Semantics and Content (https://tools.ietf.org/html/rfc7231)
// RFC7232 - Hypertext Transfer Protocol (HTTP/1.1): Conditional Requests (https://tools.ietf.org/html/rfc7232)

namespace ocst
{
	class VirtualHost;
}

namespace http
{
	namespace svr
	{
		class HttpServer : protected PhysicalPortObserver, public ov::EnableSharedFromThis<HttpServer>
		{
		protected:
			friend class HttpServerManager;

		public:
			using ClientList = std::unordered_map<ov::Socket *, std::shared_ptr<HttpConnection>>;
			using ClientIterator = std::function<bool(const std::shared_ptr<HttpConnection> &stream)>;

			HttpServer(const char *server_name, const char *server_short_name);
			~HttpServer() override;

			virtual bool Start(const ov::SocketAddress &address, int worker_count, bool enable_http2);
			virtual bool Stop();

			bool IsRunning() const;
			bool IsHttp2Enabled() const;

			bool AddInterceptor(const std::shared_ptr<RequestInterceptor> &interceptor);
			std::shared_ptr<RequestInterceptor> FindInterceptor(const std::shared_ptr<HttpExchange> &exchange);
			bool RemoveInterceptor(const std::shared_ptr<RequestInterceptor> &interceptor);

			// If the iterator returns true, FindClient() will return the client
			ov::Socket *FindClient(ClientIterator iterator);

			// If the iterator returns true, the client will be disconnected
			// And returns the number of disconnected clients
			size_t DisconnectIf(ClientIterator iterator);

		protected:
			std::shared_ptr<HttpConnection> FindClient(const std::shared_ptr<ov::Socket> &remote);
			std::shared_ptr<HttpConnection> ProcessConnect(const std::shared_ptr<ov::Socket> &remote);

			//--------------------------------------------------------------------
			// Implementation of PhysicalPortObserver
			//--------------------------------------------------------------------
			void OnConnected(const std::shared_ptr<ov::Socket> &remote) override;
			void OnDataReceived(const std::shared_ptr<ov::Socket> &remote, const ov::SocketAddress &address, const std::shared_ptr<const ov::Data> &data) override;
			void OnDisconnected(const std::shared_ptr<ov::Socket> &remote, PhysicalPortDisconnectReason reason, const std::shared_ptr<const ov::Error> &error) override;

			std::shared_ptr<PhysicalPort> GetPhysicalPort()
			{
				return _physical_port;
			}

			ov::String _server_name;
			ov::String _server_short_name;
			mutable std::mutex _physical_port_mutex;
			std::shared_ptr<PhysicalPort> _physical_port = nullptr;

			std::shared_mutex _client_list_mutex;
			ClientList _connection_list;

			std::shared_mutex _interceptor_list_mutex;
			std::vector<std::shared_ptr<RequestInterceptor>> _interceptor_list;
			std::vector<std::shared_ptr<ocst::VirtualHost>> _virtual_host_list;

		private:
			ov::DelayQueueAction Repeater(void *parameter);

			static ov::DelayQueue _repeater;

			bool _http2_enabled = true;
		};

	}  // namespace svr
}  // namespace http
