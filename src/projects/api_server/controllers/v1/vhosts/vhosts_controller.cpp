//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2020 AirenSoft. All rights reserved.
//
//==============================================================================
#include "vhosts_controller.h"

#include <config/config.h>

#include "../../../api_private.h"
#include "../../../api_server.h"
#include "apps/apps_controller.h"
#include "vhost_actions_controller.h"

namespace api
{
	namespace v1
	{
		void VHostsController::PrepareHandlers()
		{
			RegisterPost(R"()", &VHostsController::OnPostVHost);
			RegisterGet(R"()", &VHostsController::OnGetVHostList);
			RegisterGet(R"(\/(?<vhost_name>[^\/]*))", &VHostsController::OnGetVHost);
			RegisterDelete(R"(\/(?<vhost_name>[^\/]*))", &VHostsController::OnDeleteVHost);

			// Branch into action controller
			CreateSubController<VHostActionsController>(R"(:)");
			CreateSubController<VHostActionsController>(R"(\/(?<vhost_name>[^\/:]*):)");

			// Branch into apps controller
			CreateSubController<AppsController>(R"(\/(?<vhost_name>[^\/]*)\/apps)");
		}

		ApiResponse VHostsController::OnPostVHost(const std::shared_ptr<http::svr::HttpExchange> &client, const Json::Value &request_body)
		{
			if (request_body.isArray() == false)
			{
				throw http::HttpError(http::StatusCode::BadRequest, "Request body must be an array");
			}

			Json::Value response_value(Json::ValueType::arrayValue);
			const Json::Value &requested_vhost_list = request_body;
			MultipleStatus status_codes;

			for (const auto &requested_vhost : requested_vhost_list)
			{
				cfg::vhost::VirtualHost vhost_config;

				try
				{
					::serdes::VirtualHostFromJson(requested_vhost, &vhost_config);

					_server->CreateVHost(vhost_config);

					auto vhost		= GetVirtualHost(vhost_config.GetName());
					auto vhost_json = vhost->ToJson();
					vhost_json.removeMember("applications");

					Json::Value response;
					response["statusCode"] = static_cast<int>(http::StatusCode::OK);
					response["message"]	   = StringFromStatusCode(http::StatusCode::OK);
					response["response"]   = vhost_json;

					status_codes.AddStatusCode(http::StatusCode::OK);
					response_value.append(std::move(response));
				}
				catch (const cfg::ConfigError &error)
				{
					auto http_error = http::HttpError(http::StatusCode::BadRequest, error.What());

					status_codes.AddStatusCode(http_error.GetStatusCode());
					response_value.append(::serdes::JsonFromError(http_error));
				}
				catch (const http::HttpError &error)
				{
					status_codes.AddStatusCode(error.GetStatusCode());
					response_value.append(::serdes::JsonFromError(error));
				}
			}

			return {status_codes, std::move(response_value)};
		}

		ApiResponse VHostsController::OnGetVHostList(const std::shared_ptr<http::svr::HttpExchange> &client)
		{
			auto vhost_list = GetVirtualHostList();
			Json::Value response(Json::ValueType::arrayValue);

			for (const auto &item : vhost_list)
			{
				response.append(item.second->GetName().CStr());
			}

			return response;
		}

		ApiResponse VHostsController::OnGetVHost(const std::shared_ptr<http::svr::HttpExchange> &client,
												 const std::shared_ptr<mon::HostMetrics> &vhost)
		{
			return ::serdes::JsonFromVirtualHost(*vhost);
		}

		ApiResponse VHostsController::OnDeleteVHost(const std::shared_ptr<http::svr::HttpExchange> &client,
													const std::shared_ptr<mon::HostMetrics> &vhost)
		{
			ThrowIfVirtualIsReadOnly(*(vhost.get()));

			_server->DeleteVHost(*(vhost.get()));

			return {};
		}
	}  // namespace v1
}  // namespace api
