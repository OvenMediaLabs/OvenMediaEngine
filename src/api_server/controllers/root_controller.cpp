//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2020 AirenSoft. All rights reserved.
//
//==============================================================================
#include "root_controller.h"

#include <base/ovcrypto/ovcrypto.h>

#include "./v1/controller.h"

namespace api
{
	RootController::RootController(const ov::String &access_token)
		: _access_token(access_token)
	{
	}

	void RootController::PrepareHandlers()
	{
		// Prepare a handler to verify that the token sent with the "authorized" header matches the configuration of Server.xml:
		//
		// <Server>
		//     <Managers>
		//         <API>
		//             <AccessToken>ometest</AccessToken>
		//         </API>
		//     </Managers>
		// </Server>
		//
		// This handler must be installed before any other handler.
		PrepareAccessTokenHandler();

		// Currently only v1 is supported
		CreateSubController<v1::Controller>(R"(\/v1)");

		// This handler is called if it does not match all other registered handlers
		Register(http::Method::All, R"(.+)", &RootController::OnNotFound);
	}

	void RootController::PrepareAccessTokenHandler()
	{
		_interceptor->Register(http::Method::All, R"(.+)", [=](const std::shared_ptr<http::svr::HttpExchange> &client) -> http::svr::NextHandler {
#if DEBUG
			if (_access_token.IsEmpty())
			{
				// Authorization is disabled
				return http::svr::NextHandler::Call;
			}
#endif	// DEBUG

			const auto &response = client->GetResponse();

			try
			{
				auto authorization = client->GetRequest()->GetHeader("Authorization");

				if (authorization.IsEmpty())
				{
					throw http::HttpError(http::StatusCode::Unauthorized, "Authorization header is required to call API");
				}

				auto tokens = authorization.Split(" ");

				if (tokens.size() != 2)
				{
					// Invalid tokens
					throw http::HttpError(http::StatusCode::Unauthorized, "Invalid authorization header");
				}

				if (tokens[0].UpperCaseString() != "BASIC")
				{
					throw http::HttpError(http::StatusCode::Unauthorized, "Not supported credential type: %s", tokens[0].CStr());
				}

				auto data = ov::Base64::Decode(tokens[1]);

				if (data == nullptr)
				{
					throw http::HttpError(http::StatusCode::Unauthorized, "Invalid credential format");
				}

				ov::String str = data->ToString();

				if (str != _access_token)
				{
					throw http::HttpError(http::StatusCode::Unauthorized, "Invalid credential");
				}

				return http::svr::NextHandler::Call;
			}
			catch (const http::HttpError &error)
			{
				logw("APIController", "HTTP error occurred: %s", error.What());
				response->SetHeader("WWW-Authenticate", "Basic realm=\"OvenMediaEngine\"");
				ApiResponse(&error).SendToClient(client);
			}
			catch (const cfg::ConfigError &error)
			{
				logw("APIController", "Config error occurred: %s", error.GetDetailedMessage().CStr());
				response->SetHeader("WWW-Authenticate", "Basic realm=\"OvenMediaEngine\"");
				ApiResponse(&error).SendToClient(client);
			}
			catch (const std::exception &error)
			{
				logw("APIController", "Unknown error occurred: %s", error.what());
				response->SetHeader("WWW-Authenticate", "Basic realm=\"OvenMediaEngine\"");
				ApiResponse(&error).SendToClient(client);
			}

			return http::svr::NextHandler::DoNotCall;
		});
	}

	ApiResponse RootController::OnNotFound(const std::shared_ptr<http::svr::HttpExchange> &client)
	{
		throw http::HttpError(http::StatusCode::NotFound, "Controller not found");
	}
}  // namespace api
