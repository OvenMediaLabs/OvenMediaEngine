//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================
#include "http_default_interceptor.h"

#include "./http_server_private.h"
#include "http_exchange.h"

// Currently, OME does not handle requests larger than 1 MB
#define MAX_HTTP_REQUEST_SIZE (1024LL * 1024LL)

namespace http
{
	namespace svr
	{
		DefaultInterceptor::DefaultInterceptor(const ov::String &pattern_prefix)
			: _pattern_prefix(pattern_prefix)
		{
		}

		bool DefaultInterceptor::Register(Method method, const ov::String &pattern, const RequestHandler &handler)
		{
			if (handler == nullptr)
			{
				return false;
			}

			ov::String whole_pattern;
			whole_pattern.Format("^%s%s$", _pattern_prefix.CStr(), pattern.CStr());

			auto regex = ov::Regex(whole_pattern);
			auto error = regex.Compile();

			if (error == nullptr)
			{
				// if mehotd == GET, set HEAD automatically
				if (HTTP_CHECK_METHOD(method, Method::Get))
				{
					method |= Method::Head;
				}

				_request_handler_list.push_back((RequestInfo) {
#if DEBUG
					.pattern_string = whole_pattern,
#endif	// DEBUG
					.pattern = std::move(regex),
					.method = method,
					.handler = handler
				});
			}
			else
			{
				logte("Invalid regex pattern: %s (Error: %s)", pattern.CStr(), error->What());
				return false;
			}

			return true;
		}

		bool DefaultInterceptor::IsInterceptorForRequest(const std::shared_ptr<const HttpExchange> &client)
		{
			// Process all requests because this is a default interceptor
			return true;
		}

		bool DefaultInterceptor::IsCacheable() const
		{
			return true;
		}

		bool DefaultInterceptor::OnRequestPrepared(const std::shared_ptr<HttpExchange> &exchange)
		{
			return true;
		}

		bool DefaultInterceptor::OnDataReceived(const std::shared_ptr<HttpExchange> &exchange, const std::shared_ptr<const ov::Data> &data)
		{
			auto request = exchange->GetRequest();
			const std::shared_ptr<ov::Data> &request_body = GetRequestBody(request);

			request_body->Append(data);
			
			return true;
		}

		InterceptorResult DefaultInterceptor::OnRequestCompleted(const std::shared_ptr<HttpExchange> &exchange)
		{
			auto request = exchange->GetRequest();
			auto response = exchange->GetResponse();
			int handler_count = 0;

			// A variable to handle 403 Method not allowed
			bool regex_found = false;

			auto uri = ov::Url::Parse(request->GetUri());

			if (uri == nullptr)
			{
				logte("Could not parse uri: %s", request->GetUri().CStr());
				
				response->SetStatusCode(StatusCode::BadRequest);
				response->Response();
				exchange->Release();

				return InterceptorResult::Error;
			}

			auto uri_target = uri->Path();

			for (auto &request_info : _request_handler_list)
			{
#if DEBUG
				logtt("Check if url [%s] is matches [%s]", uri_target.CStr(), request_info.pattern_string.CStr());
#endif	// DEBUG

				response->SetStatusCode(StatusCode::OK);

				auto matches = request_info.pattern.Matches(uri_target);
				auto error = matches.GetError();

				if (error == nullptr)
				{
#if DEBUG
					logtt("Matches: url [%s], pattern: [%s]", uri_target.CStr(), request_info.pattern_string.CStr());
#endif	// DEBUG

					regex_found = true;

					if (HTTP_CHECK_METHOD(request_info.method, request->GetMethod()))
					{
						handler_count++;

						request->SetMatchResult(matches);
						
						auto request_result = request_info.handler(exchange);
						if (request_result == NextHandler::DoNotCall)
						{
							break;
						}
						else if (request_result == NextHandler::DoNotCallAndDoNotResponse)
						{
							return InterceptorResult::Moved;
						}

						// Call the next handler
					}
				}
				else
				{
#if DEBUG
					logtt("Not matched: url [%s], pattern: [%s] (with error: %s)", uri_target.CStr(), request_info.pattern_string.CStr(), error->What());
#endif	// DEBUG
				}
			}

			if (handler_count == 0)
			{
				if (regex_found)
				{
					// Handler matching the pattern was found, but no matching method was found
					response->SetStatusCode(StatusCode::MethodNotAllowed);
				}
				else
				{
					// Handler not found
					response->SetStatusCode(StatusCode::NotFound);
				}
			}
			else
			{
				// Everything is OK
			}

			response->Response();
			exchange->Release();
			return InterceptorResult::Completed;
		}

		void DefaultInterceptor::OnClosed(const std::shared_ptr<HttpConnection> &connection, PhysicalPortDisconnectReason reason)
		{
			if (_close_handler != nullptr)
			{
				_close_handler(connection, reason);
			}
		}
	}  // namespace svr
}  // namespace http
