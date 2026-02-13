//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2025 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <base/ovlibrary/ovlibrary.h>
#include <base/ovsocket/ovsocket.h>

#include "../http_datastructure.h"
#include "../http_error.h"
#include "../protocol/http1/http_response_parser.h"
#include "./interceptors/http_client_interceptors.h"

namespace http
{
	namespace clnt
	{
		constexpr int TIMEOUT_INFINITE = 0;

		class HttpClientV2 : public ov::EnableSharedFromThis<HttpClientV2>,
							 public ov::SocketAsyncInterface,
							 public ov::TlsClientDataIoCallback
		{
		protected:
			struct PrivateToken
			{
			};

		public:
			// Use default TCP socket pool
			HttpClientV2(PrivateToken token);
			// Use specified socket pool
			HttpClientV2(PrivateToken token, const std::shared_ptr<ov::SocketPool> &socket_pool);

			static std::shared_ptr<HttpClientV2> Create()
			{
				return std::make_shared<HttpClientV2>(PrivateToken{});
			}

			static std::shared_ptr<HttpClientV2> Create(const std::shared_ptr<ov::SocketPool> &socket_pool)
			{
				return std::make_shared<HttpClientV2>(PrivateToken{}, socket_pool);
			}

			~HttpClientV2() override;

			void SetBlockingMode(ov::BlockingMode mode);
			ov::BlockingMode GetBlockingMode() const;

			// Use `TIMEOUT_INFINITE` to set infinite timeout if needed
			void SetConnectionTimeout(int timeout_msec);
			int GetConnectionTimeout() const;

			// Use `TIMEOUT_INFINITE` to set infinite timeout if needed
			void SetRecvTimeout(int timeout_msec);
			int GetRecvTimeout() const;

			void SetMethod(Method method);
			Method GetMethod() const;

			void AddInterceptor(const std::shared_ptr<HttpClientInterceptor> &interceptor)
			{
				std::unique_lock lock(_interceptor_list_mutex);
				_interceptor_list.push_back(interceptor);
			}

			void ClearInterceptors()
			{
				std::unique_lock lock(_interceptor_list_mutex);
				_interceptor_list.clear();
			}

			// Request headers (Headers to be sent to the HTTP server)
			void SetRequestHeader(const ov::String &key, const ov::String &value);
			std::optional<ov::String> GetRequestHeader(const ov::String &key);
			const HttpHeaderMap GetRequestHeaders() const;
			void RemoveRequestHeader(const ov::String &key);

			// HttpClientV2 can send a request body even when the method is GET, but the server may not actually accept it
			void SetRequestBody(const std::shared_ptr<const ov::Data> &body);
			void SetRequestBody(const ov::String &body)
			{
				SetRequestBody(body.ToData(false));
			}
			const std::shared_ptr<const ov::Data> GetRequestBody() const;

			// If the request is made successfully in non-blocking mode, it returns a `CancelToken`,
			// otherwise, it returns `nullptr` in blocking mode or when an error occurs during the request.
			std::shared_ptr<CancelToken> Request(const ov::String &url);

			auto &GetParser() const
			{
				return _parser;
			}

			// Response headers (Headers received from HTTP server)
			std::optional<ov::String> GetResponseHeader(const ov::String &key) const;
			const HttpHeaderMap &GetResponseHeaders() const;

		protected:
			enum class ChunkParseStatus
			{
				None,

				// Waiting for chunk header
				Header,
				// Waiting for chunk body
				Body,
				// Waiting for \r
				CarriageReturn,
				// Waiting for \n
				LineFeed,
				// Waiting for \r (to determine end of response)
				CarriageReturnEnd,
				// Waiting for \n (to determine end of response)
				LineFeedEnd,

				Completed
			};

		protected:
			//--------------------------------------------------------------------
			// Implementation of SocketAsyncInterface
			//--------------------------------------------------------------------
			void OnConnected(const std::shared_ptr<const ov::SocketError> &error) override;
			void OnReadable() override;
			void OnClosed() override;

		protected:
			//--------------------------------------------------------------------
			// Implementation of ov::TlsClientDataIoCallback
			//--------------------------------------------------------------------
			ssize_t OnTlsReadData(void *data, int64_t length) override;
			ssize_t OnTlsWriteData(const void *data, int64_t length) override;

		protected:
			std::shared_ptr<const ov::Error> PrepareForRequest(const ov::String &url, ov::SocketAddress *address);
			std::shared_ptr<const ov::OpensslError> TryTlsConnect();

			// Calls `OnClosed()` (if needed) and `OnRequestFinished()`
			void CallCloseFinish();

			void SendRequestIfNeeded();
			// Use this API when blocking mode
			void RecvResponse();
			std::shared_ptr<const ov::Error> ProcessChunk(const std::shared_ptr<const ov::Data> &data, size_t *processed_bytes);
			std::shared_ptr<const ov::Error> ProcessData(const std::shared_ptr<const ov::Data> &data);

			bool SendData(const std::shared_ptr<const ov::Data> &data);

			std::shared_ptr<const ov::Error> HandleParseCompleted();
			void CleanupVariables();

			void HandleError(std::shared_ptr<const ov::Error> error);

		protected:
			std::shared_ptr<ov::SocketPool> _socket_pool;

			ov::BlockingMode _blocking_mode = ov::BlockingMode::Blocking;
			// Default: 10 seconds
			int _connection_timeout_msec	= 10 * 1000;
			// Default: 60 seconds
			int _recv_timeout_msec			= 60 * 1000;
			Method _method					= Method::Get;

			std::shared_mutex _interceptor_list_mutex;
			std::vector<std::shared_ptr<HttpClientInterceptor>> _interceptor_list;

			bool _connected_callback_called = false;
#ifdef DEBUG
			bool _request_finished_callback_called = false;
#endif	// DEBUG

			// Related to chunked transfer
			bool _is_chunked_transfer			 = false;
			ChunkParseStatus _chunk_parse_status = ChunkParseStatus::None;
			// Length of the chunk (include \r\n)
			size_t _chunk_length				 = 0L;
			ov::String _chunk_header;

			std::shared_ptr<ov::TlsClientData> _tls_data;

			prot::h1::HttpResponseParser _parser;

			mutable std::recursive_mutex _request_mutex;
			std::atomic<bool> _requested = false;

			ov::String _url;
			std::shared_ptr<ov::Url> _parsed_url;

			std::shared_ptr<ov::Socket> _socket;

			HttpHeaderMap _request_header_map;
			std::shared_ptr<ov::Data> _request_body;

			// response headers
			HttpHeaderMap _response_header_map;
			// Total received body size
			size_t _response_body_size = 0;
		};
	}  // namespace clnt
}  // namespace http
