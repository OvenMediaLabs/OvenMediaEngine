//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2020 AirenSoft. All rights reserved.
//
//==============================================================================
#include "apps_controller.h"

#include <config/config.h>
#include <modules/json_serdes/converters.h>
#include <orchestrator/orchestrator.h>

#include <functional>

#include "../../../../api_private.h"
#include "app_actions_controller.h"
#include "multiplex_channels/multiplex_channels_controller.h"
#include "output_profiles/output_profiles_controller.h"
#include "scheduled_channels/scheduled_channels_controller.h"
#include "streams/streams_controller.h"

namespace api
{
	namespace v1
	{
		void AppsController::PrepareHandlers()
		{
			RegisterPost(R"()", &AppsController::OnPostApp);
			RegisterGet(R"()", &AppsController::OnGetAppList);
			RegisterGet(R"(\/(?<app_name>[^\/:]*))", &AppsController::OnGetApp);
			RegisterPatch(R"(\/(?<app_name>[^\/:]*))", &AppsController::OnPatchApp);
			RegisterDelete(R"(\/(?<app_name>[^\/:]*))", &AppsController::OnDeleteApp);

			// Branch into action controller
			CreateSubController<AppActionsController>(R"(\/(?<app_name>[^\/:]*):)");

			// Branch into stream controller
			CreateSubController<StreamsController>(R"(\/(?<app_name>[^\/:]*)\/streams)");

			CreateSubController<ScheduledChannelsController>(R"(\/(?<app_name>[^\/:]*)\/scheduledChannels)");
			CreateSubController<MultiplexChannelsController>(R"(\/(?<app_name>[^\/:]*)\/multiplexChannels)");

			// Branch into output profile controller
			CreateSubController<OutputProfilesController>(R"(\/(?<app_name>[^\/:]*)\/outputProfiles)");
		};

		ApiResponse AppsController::OnPostApp(const std::shared_ptr<http::svr::HttpExchange> &client, const Json::Value &request_body,
											  const std::shared_ptr<mon::HostMetrics> &vhost)
		{
			ThrowIfVirtualIsReadOnly(*(vhost.get()));

			if (request_body.isArray() == false)
			{
				throw http::HttpError(http::StatusCode::BadRequest, "Request body must be an array");
			}

			Json::Value response_value(Json::ValueType::arrayValue);
			// Copy values to fill default values
			Json::Value requested_app_list = request_body;

			MultipleStatus status_codes;

			for (auto &requested_app : requested_app_list)
			{
				FillDefaultAppConfigValues(requested_app);

				cfg::vhost::app::Application app_config;
				::serdes::ApplicationFromJson(requested_app, &app_config);

				try
				{
					app_config.FromJson(requested_app);

					ThrowIfOrchestratorNotSucceeded(
						ocst::Orchestrator::GetInstance()->CreateApplication(*vhost, app_config),
						"create",
						"application",
						ov::String::FormatString("%s/%s", vhost->GetName().CStr(), app_config.GetName().CStr()));

					auto app	  = GetApplication(vhost, app_config.GetName().CStr());
					auto app_json = ::serdes::JsonFromApplication(app);

					Json::Value response;
					response["statusCode"] = static_cast<int>(http::StatusCode::OK);
					response["message"]	   = StringFromStatusCode(http::StatusCode::OK);
					response["response"]   = app_json;

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

		ApiResponse AppsController::OnGetAppList(const std::shared_ptr<http::svr::HttpExchange> &client,
												 const std::shared_ptr<mon::HostMetrics> &vhost)
		{
			Json::Value response(Json::ValueType::arrayValue);

			auto app_list = GetApplicationList(vhost);

			for (auto &item : app_list)
			{
				auto &app = item.second;

				response.append(app->GetVHostAppName().GetAppName().CStr());
			}

			return response;
		}

		ApiResponse AppsController::OnGetApp(const std::shared_ptr<http::svr::HttpExchange> &client,
											 const std::shared_ptr<mon::HostMetrics> &vhost,
											 const std::shared_ptr<mon::ApplicationMetrics> &app)
		{
			return ::serdes::JsonFromApplication(app);
		}

		ApiResponse AppsController::OnPatchApp(const std::shared_ptr<http::svr::HttpExchange> &client, const Json::Value &request_body,
											   const std::shared_ptr<mon::HostMetrics> &vhost,
											   const std::shared_ptr<mon::ApplicationMetrics> &app)
		{
			ThrowIfVirtualIsReadOnly(*(vhost.get()));

			if (request_body.isObject() == false)
			{
				throw http::HttpError(http::StatusCode::BadRequest, "Request body must be an object");
			}

			// TODO(dimiden): Caution - Race condition may occur
			// If an application is deleted immediately after the GetApplication(),
			// the app information can no longer be obtained from Orchestrator
			auto app_json = ::serdes::JsonFromApplication(app);

			// Delete GET-only fields
			app_json.removeMember("dynamic");

			// Prevent to change the name/outputProfiles using this API
			if (request_body.isMember("name"))
			{
				throw http::HttpError(http::StatusCode::BadRequest, "Cannot change [name] using this API");
			}

			if (request_body.isMember("dynamic"))
			{
				throw http::HttpError(http::StatusCode::BadRequest, "Cannot change [dynamic] using this API");
			}

			if (request_body.isMember("outputProfiles"))
			{
				throw http::HttpError(http::StatusCode::BadRequest, "Cannot change [outputProfiles] using this API");
			}

			// Copy request_body into app_json
			OverwriteJson(request_body, app_json);

			cfg::vhost::app::Application app_config;
			::serdes::ApplicationFromJson(app_json, &app_config);

			ThrowIfOrchestratorNotSucceeded(
				ocst::Orchestrator::GetInstance()->DeleteApplication(*app),
				"delete",
				"application",
				ov::String::FormatString("%s/%s", vhost->GetName().CStr(), app->GetVHostAppName().GetAppName().CStr()));

			ThrowIfOrchestratorNotSucceeded(
				ocst::Orchestrator::GetInstance()->CreateApplication(*vhost, app_config),
				"create",
				"application",
				ov::String::FormatString("%s/%s", vhost->GetName().CStr(), app->GetVHostAppName().GetAppName().CStr()));

			auto app_metrics = GetApplication(vhost, app_config.GetName().CStr());

			return ::serdes::JsonFromApplication(app_metrics);
		}

		ApiResponse AppsController::OnDeleteApp(const std::shared_ptr<http::svr::HttpExchange> &client,
												const std::shared_ptr<mon::HostMetrics> &vhost,
												const std::shared_ptr<mon::ApplicationMetrics> &app)
		{
			ThrowIfVirtualIsReadOnly(*(vhost.get()));

			ThrowIfOrchestratorNotSucceeded(
				ocst::Orchestrator::GetInstance()->DeleteApplication(*app),
				"delete",
				"application",
				ov::String::FormatString("%s/%s", vhost->GetName().CStr(), app->GetVHostAppName().GetAppName().CStr()));

			return {};
		}
	}  // namespace v1
}  // namespace api
