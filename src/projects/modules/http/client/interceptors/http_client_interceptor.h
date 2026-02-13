//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2025 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include "../../protocol/http1/http_response_parser.h"
#include "../http_client_data_structure.h"

namespace http
{
	namespace clnt
	{
		// `ResponseInfo` is immutable
		class ResponseInfo
		{
		protected:
			struct PrivateToken
			{
			};

		public:
			ResponseInfo(PrivateToken token, const http::prot::h1::HttpResponseParser &parser)
				: _status_code(parser.GetStatusCode()),
				  _content_length(parser.HasContentLength() ? parser.GetContentLength() : static_cast<decltype(_content_length)>(std::nullopt)),
				  _header_map(parser.GetHeaders())
			{
			}

			static std::shared_ptr<const ResponseInfo> From(const http::prot::h1::HttpResponseParser &parser)
			{
				return std::make_shared<ResponseInfo>(PrivateToken{}, parser);
			}

			StatusCode GetStatusCode() const
			{
				return _status_code;
			}

			std::optional<size_t> GetContentLength() const
			{
				return _content_length;
			}

			const HttpHeaderMap &GetHeaders() const
			{
				return _header_map;
			}

			std::optional<ov::String> GetHeader(const ov::String &key) const
			{
				auto iterator = _header_map.find(key);
				if (iterator != _header_map.end())
				{
					return iterator->second;
				}
				return std::nullopt;
			}

		protected:
			StatusCode _status_code;
			std::optional<size_t> _content_length;
			HttpHeaderMap _header_map;
		};

		/**
		 * @brief
		 * The handlers of HttpClientInterceptor are called in the following order:
		 * 
		 * ```
		 *        (Initialization)
		 *               │
		 *               │ Call when the request is initiated
		 *               ▼
		 *       `OnRequestStarted()`
		 *               │
		 *               ├─────────▶ DNS resolution/connection failure, etc. ────▶ `OnError()` ───────┐
		 *               ├─────────▶ Canceled by the user ───────────────────────▶ `OnCanceled()` ────┤
		 *               │                                                                            │
		 *               │ Connection is established                                                  │
		 *               ▼                                                                            │
		 *        `OnConnected()`                                                                     │
		 *               │                                                                            │
		 *               ├─────────▶ An error occurred ─────────────────────────────────┐             │
		 *               ├─────────▶ Canceled by the user ────────────────────┐         │             │
		 *               │                                                    │         │             │
		 *               │ HTTP header is parsed                              │         │             │
		 *               ▼                                                    │         │             │
		 *        `OnResponseInfo()`                                          │         │             │
		 *               │                                                    │         │             │
		 *               ├─────────▶ An error occurred ───────────────────────│─────────┤             │
		 *               ├─────────▶ Canceled by the user ────────────────────┤         │             │
		 *               │                                                    │         │             │
		 *               ├─────────▶ No body (Content-Length == 0) ───┐       │         │             │
		 *               │                                            │       │         │             │
		 *               │ Data is received                           │       │         │             │
		 *               ▼                                            │       │         │             │
		 *       ┌─▶ `OnData()` (0..N times)                          │       │         │             │
		 *  More │       │                                            │       │         ▼             │
		 *  Data │       ├─────────▶ An error occurs ─────────────────│───────│──▶ `OnError()` ─────┐ │
		 *       │       ├─────────▶ Canceled by the user ────────────│───────┴──▶ `OnCanceled()` ──┤ │
		 *       └───────┤                                            │                             │ │
		 *               │                                            │                             │ │
		 *               │ All data is received                       │   Connection is closed      │ │
		 *               ├────────────────────────────────────────────┘                             │ │
		 *               ▼                                                                          │ │
		 *        `OnCompleted()`                                                                   │ │
		 *               │                                                                          │ │
		 *               │ Connection is closed                                                     │ │
		 *               ├──────────────────────────────────────────────────────────────────────────┘ │
		 *               ▼                                                                            │
		 *          `OnClosed()`                                                                      │
		 *               │                                                                            │
		 *               ├────────────────────────────────────────────────────────────────────────────┘
		 *               ▼
		 *     `OnRequestFinished()`
		 * ```
		 * 
		 * @note
		 * `OnRequestStarted()` may be called from a different thread than other callbacks, so synchronization is required if necessary.
		 */
		class HttpClientInterceptor
		{
			friend class HttpClientV2;

		public:
			virtual ~HttpClientInterceptor() = default;

			void SetCancelToken(std::shared_ptr<CancelToken> cancel_token)
			{
				_cancel_token = std::move(cancel_token);
			}

		protected:
			/**
			 * @brief
			 * Called when the HTTP request is initiated.
			 *
			 * @param url The URL to which the request is being made. If you want to change the request URL, modify this object.
			 *
			 * @returns
			 * If you want to cancel the request, return a `std::shared_ptr<ov::Error>`.
			 */
			virtual std::shared_ptr<const ov::Error> OnRequestStarted(const std::shared_ptr<ov::Url> &url)
			{
				return nullptr;
			}

			/**
			 * @brief
			 * Called when connected to the server
			 *
			 * @returns
			 * If you want to cancel the request, return a `std::shared_ptr<ov::Error>`.
			 */
			virtual std::shared_ptr<const ov::Error> OnConnected()
			{
				return nullptr;
			}

			/**
			 * @brief
			 * Called when HTTP header parsing is completed.
			 *
			 * @param response_info Parsed HTTP response
			 *
			 * @returns
			 * If you want to cancel the request, return a `std::shared_ptr<ov::Error>`.
			 */
			virtual std::shared_ptr<const ov::Error> OnResponseInfo(const std::shared_ptr<const ResponseInfo> &response_info)
			{
				return nullptr;
			}

			/**
			 * @brief
			 * Called when the HTTP response body is received.
			 *
			 * @param data Received data
			 *
			 * @returns
			 * If you want to cancel the request, return a `std::shared_ptr<ov::Error>`.
			 */
			virtual std::shared_ptr<const ov::Error> OnData(const std::shared_ptr<const ov::Data> &data)
			{
				return nullptr;
			}

			/**
			 * @brief
			 * Called when the HTTP request is completed.
			 *
			 * @note
			 * For a single request, only one of `OnCompleted()`, `OnError()`, or `OnCanceled()` will be invoked.
			 */
			virtual void OnCompleted() {}

			/**
			 * @brief
			 * Called when an error occurs during connection, header parsing, or data reception
			 *
			 * @param error Error information
			 *
			 * @note
			 * For a single request, only one of `OnCompleted()`, `OnError()`, or `OnCanceled()` will be invoked.
			 */
			virtual void OnError(const std::shared_ptr<const ov::Error> &error) {}

			/**
			 * @brief
			 * Called when the request is canceled by the user.
			 *
			 * @note
			 * For a single request, only one of `OnCompleted()`, `OnError()`, or `OnCanceled()` will be invoked.
			 */
			virtual void OnCanceled() {}

			/**
			 * @brief
			 * Called when the connection is closed.
			 *
			 * @note
			 * This callback is called only when `OnConnected()` is called.
			 * If `OnConnected()` is not called, this callback will not be called.
			 */
			virtual void OnClosed() {}

			/**
			 * @brief
			 * Called when the request is finished, regardless of success or failure
			 *
			 * @note
			 * This callback is called after `OnError()`, `OnClosed()`, or `OnCompleted()`.
			 * It is guaranteed to be called once per request.
			 */
			virtual void OnRequestFinished() {}

		protected:
			std::shared_ptr<CancelToken> _cancel_token;
		};
	}  // namespace clnt
}  // namespace http
