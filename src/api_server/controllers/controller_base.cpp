//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2020 AirenSoft. All rights reserved.
//
//==============================================================================
#include "./controller_base.h"

namespace api
{
	ApiResponse::ApiResponse()
		: ApiResponse(http::StatusCode::OK)
	{
	}

	ApiResponse::ApiResponse(bool deferred)
		: _is_deferred(deferred)
	{
	}

	ApiResponse::ApiResponse(http::StatusCode status_code)
	{
		SetResponse(status_code);
	}

	ApiResponse::ApiResponse(http::StatusCode status_code, const Json::Value &json)
	{
		SetResponse(status_code, nullptr, json);
	}

	ApiResponse::ApiResponse(MultipleStatus status_codes, const Json::Value &json)
	{
		if (json.isArray())
		{
			_status_code = status_codes.GetStatusCode();
			_json		 = json;
		}
		else
		{
			SetResponse(status_codes.GetStatusCode(), nullptr, json);
		}
	}

	ApiResponse::ApiResponse(const Json::Value &json)
	{
		SetResponse(http::StatusCode::OK, nullptr, json);
	}

	ApiResponse::ApiResponse(const std::exception *error)
	{
		if (error == nullptr)
		{
			SetResponse(http::StatusCode::OK);
			return;
		}

		auto http_error = dynamic_cast<const http::HttpError *>(error);
		if (http_error != nullptr)
		{
			SetResponse(http_error->GetStatusCode(), http_error->What());
			return;
		}

		auto config_error = dynamic_cast<const cfg::ConfigError *>(error);
		if (config_error != nullptr)
		{
			SetResponse(http::StatusCode::BadRequest, config_error->What());
			return;
		}

		SetResponse(http::StatusCode::InternalServerError, error->what());
	}

	ApiResponse::ApiResponse(const ApiResponse &response)
	{
		_status_code = response._status_code;
		_json		 = response._json;
	}

	ApiResponse::ApiResponse(ApiResponse &&response)
	{
		_status_code = std::move(response._status_code);
		_json		 = std::move(response._json);
	}

	void ApiResponse::SetResponse(http::StatusCode status_code)
	{
		SetResponse(status_code, nullptr);
	}

	void ApiResponse::SetResponse(http::StatusCode status_code, const char *message)
	{
		_status_code		= status_code;

		_json["statusCode"] = static_cast<int>(status_code);
		_json["message"]	= (message == nullptr) ? StringFromStatusCode(status_code) : message;
	}

	void ApiResponse::SetResponse(http::StatusCode status_code, const char *message, const Json::Value &json)
	{
		SetResponse(status_code, message);

		_json["response"] = json;
	}

	bool ApiResponse::SendToClient(const std::shared_ptr<http::svr::HttpExchange> &client) const
	{
		const auto &response = client->GetResponse();

		response->SetStatusCode(_status_code);
		response->SetHeader("Content-Type", "application/json;charset=UTF-8");

		return (_json.isNull() == false) ? response->AppendString(ov::Json::Stringify(_json)) : true;
	}
}  // namespace api
