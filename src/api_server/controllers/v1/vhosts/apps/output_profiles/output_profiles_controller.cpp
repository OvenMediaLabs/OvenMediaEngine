//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2020 AirenSoft. All rights reserved.
//
//==============================================================================
#include "output_profiles_controller.h"

#include <orchestrator/orchestrator.h>

#include <functional>

#include "../../../../../api_private.h"

#define CreateNotFoundError(vhost_metrics, app_metrics, output_profile_name) \
	http::HttpError(                                                         \
		http::StatusCode::NotFound,                                          \
		"Could not find the output profile: [%s/%s/%s]",                     \
		vhost_metrics->GetName().CStr(),                                     \
		app_metrics->GetVHostAppName().GetAppName().CStr(),                  \
		output_profile_name.CStr())

namespace api
{
	namespace v1
	{
		void OutputProfilesController::PrepareHandlers()
		{
			RegisterPost(R"()", &OutputProfilesController::OnPostOutputProfile);
			RegisterGet(R"()", &OutputProfilesController::OnGetOutputProfileList);
			RegisterGet(R"(\/(?<output_profile_name>[^\/]*))", &OutputProfilesController::OnGetOutputProfile);
			RegisterPatch(R"(\/(?<output_profile_name>[^\/]*))", &OutputProfilesController::OnPatchOutputProfile);
			RegisterDelete(R"(\/(?<output_profile_name>[^\/]*))", &OutputProfilesController::OnDeleteOutputProfile);
		};

		ApiResponse OutputProfilesController::OnPostOutputProfile(const std::shared_ptr<http::svr::HttpExchange> &client, const Json::Value &request_body,
																  const std::shared_ptr<mon::HostMetrics> &vhost,
																  const std::shared_ptr<mon::ApplicationMetrics> &app)
		{
			ThrowIfVirtualIsReadOnly(*(vhost.get()));

			if (request_body.isArray() == false)
			{
				throw http::HttpError(http::StatusCode::BadRequest, "Request body must be an array");
			}

			MultipleStatus status_codes;

			Json::Value app_json  = app->GetConfig().ToJson();
			auto &output_profiles = app_json["outputProfiles"]["outputProfile"];

			Json::Value response(Json::ValueType::arrayValue);

			for (auto &item : request_body)
			{
				// validate name, outputStreamName, encodes
				if (item.isMember("name") == false || item.isMember("outputStreamName") == false || item.isMember("encodes") == false)
				{
					throw http::HttpError(http::StatusCode::BadRequest, "Invalid request body: \"name\", \"outputStreamName\", \"encodes\" are required");
				}

				auto name			  = item["name"];
				auto outputStreamName = item["outputStreamName"];
				auto encodes		  = item["encodes"];

				if (name.isString() == false || outputStreamName.isString() == false || encodes.isObject() == false)
				{
					throw http::HttpError(http::StatusCode::BadRequest, "Invalid request body: \"name\" must be a string and \"encodes\" must be an object");
				}

				// encodes must have at least one item (videos array or audios array)
				if (encodes.isMember("videos") == false && encodes.isMember("audios") == false)
				{
					throw http::HttpError(http::StatusCode::BadRequest, "Invalid request body: \"encodes\" must have at least one item (videos array or audios array)");
				}

				size_t encodes_count = 0;
				if (encodes.isMember("videos"))
				{
					auto videos = encodes["videos"];
					if (videos.isArray() == false)
					{
						throw http::HttpError(http::StatusCode::BadRequest, "Invalid request body: \"encodes.videos\" must be an array");
					}

					encodes_count += videos.size();
				}

				if (encodes.isMember("audios"))
				{
					auto audios = encodes["audios"];
					if (audios.isArray() == false)
					{
						throw http::HttpError(http::StatusCode::BadRequest, "Invalid request body: \"encodes.audios\" must be an array");
					}

					encodes_count += audios.size();
				}

				if (encodes_count == 0)
				{
					throw http::HttpError(http::StatusCode::BadRequest, "Invalid request body: \"encodes\" must have at least one item (videos array or audios array) and each item must have at least one item");
				}

				try
				{
					if (name.isString())
					{
						if (FindOutputProfile(app_json, name.asCString(), nullptr) < 0)
						{
							output_profiles.append(item);

							response.append(name);
							status_codes.AddStatusCode(http::StatusCode::OK);
						}
						else
						{
							throw http::HttpError(http::StatusCode::Conflict, "The output profile \"%s\" already exists", name.asCString());
						}
					}
					else
					{
						throw http::HttpError(http::StatusCode::BadRequest, "Invalid name");
					}
				}
				catch (const http::HttpError &error)
				{
					status_codes.AddStatusCode(error.GetStatusCode());
					response.append(::serdes::JsonFromError(error));
				}
			}

			RecreateApplication(vhost, app, app_json);

			std::shared_ptr<mon::ApplicationMetrics> new_app;
			Json::Value new_app_json;

			new_app		 = GetApplication(vhost, app->GetVHostAppName().GetAppName().CStr());
			new_app_json = new_app->GetConfig().ToJson();

			for (auto &response_profile : response)
			{
				try
				{
					if (response_profile.isString() == false)
					{
						// An error created above step
					}
					else
					{
						auto name = response_profile.asCString();
						Json::Value *new_profile;

						if (FindOutputProfile(new_app_json, name, &new_profile) >= 0)
						{
							response_profile			   = Json::objectValue;
							response_profile["statusCode"] = static_cast<int>(http::StatusCode::OK);
							response_profile["message"]	   = StringFromStatusCode(http::StatusCode::OK);
							response_profile["response"]   = *new_profile;
						}
						else
						{
							throw http::HttpError(http::StatusCode::InternalServerError, "Output profile is created, but not found");
						}
					}
				}
				catch (const http::HttpError &error)
				{
					response_profile = ::serdes::JsonFromError(error);
					status_codes.AddStatusCode(error.GetStatusCode());
				}
			}

			return {status_codes, std::move(response)};
		}

		ApiResponse OutputProfilesController::OnGetOutputProfileList(const std::shared_ptr<http::svr::HttpExchange> &client,
																	 const std::shared_ptr<mon::HostMetrics> &vhost,
																	 const std::shared_ptr<mon::ApplicationMetrics> &app)
		{
			Json::Value response = Json::arrayValue;

			for (auto &item : app->GetConfig().GetOutputProfileList())
			{
				response.append(item.GetName().CStr());
			}

			return response;
		}

		ApiResponse OutputProfilesController::OnGetOutputProfile(const std::shared_ptr<http::svr::HttpExchange> &client,
																 const std::shared_ptr<mon::HostMetrics> &vhost,
																 const std::shared_ptr<mon::ApplicationMetrics> &app)
		{
			auto profile_name = GetOutputProfileName(client);

			for (auto &profile : app->GetConfig().GetOutputProfileList())
			{
				if (profile_name == profile.GetName().CStr())
				{
					return profile.ToJson();
				}
			}

			throw CreateNotFoundError(vhost, app, profile_name);
		}

		ApiResponse OutputProfilesController::OnPatchOutputProfile(const std::shared_ptr<http::svr::HttpExchange> &client,
																   const Json::Value &request_body,
																   const std::shared_ptr<mon::HostMetrics> &vhost,
																   const std::shared_ptr<mon::ApplicationMetrics> &app)
		{
			ThrowIfVirtualIsReadOnly(*(vhost.get()));

			if (request_body.isObject() == false)
			{
				throw http::HttpError(http::StatusCode::BadRequest, "Request body must be an object");
			}

			auto profile_name	 = GetOutputProfileName(client);
			Json::Value app_json = app->GetConfig().ToJson();
			Json::Value *output_profile_json;

			off_t index = FindOutputProfile(app_json, profile_name, &output_profile_json);

			if (index < 0)
			{
				throw CreateNotFoundError(vhost, app, profile_name);
			}

			for (auto item = request_body.begin(); item != request_body.end(); ++item)
			{
				ov::String name = item.name().c_str();
				auto lower_name = name.LowerCaseString();

				// Prevent to change the name using this API
				if (lower_name == "name")
				{
					throw http::HttpError(http::StatusCode::BadRequest, "The %s entry cannot be specified in the modification", name.CStr());
				}
			}

			Json::Value request_json = request_body;

			request_json["name"]	 = std::string(profile_name);

			// Modify the json object
			*output_profile_json	 = request_json;

			RecreateApplication(vhost, app, app_json);

			auto new_app = GetApplication(vhost, app->GetVHostAppName().GetAppName().CStr());

			Json::Value value;

			if (FindOutputProfile(new_app, profile_name, &value) < 0)
			{
				http::HttpError(http::StatusCode::InternalServerError, "Output profile is modified, but not found");
			}

			return value;
		}

		ApiResponse OutputProfilesController::OnDeleteOutputProfile(const std::shared_ptr<http::svr::HttpExchange> &client,
																	const std::shared_ptr<mon::HostMetrics> &vhost,
																	const std::shared_ptr<mon::ApplicationMetrics> &app)
		{
			ThrowIfVirtualIsReadOnly(*(vhost.get()));

			auto profile_name = GetOutputProfileName(client);
			off_t index		  = FindOutputProfile(app, profile_name, nullptr);

			if (index < 0)
			{
				throw CreateNotFoundError(vhost, app, profile_name);
			}

			Json::Value app_json  = app->GetConfig().ToJson();
			auto &output_profiles = app_json["outputProfiles"]["outputProfile"];

			if (output_profiles.removeIndex(index, nullptr) == false)
			{
				throw http::HttpError(http::StatusCode::Forbidden, "Could not delete output profile");
			}

			RecreateApplication(vhost, app, app_json);

			return {};
		}
	}  // namespace v1
}  // namespace api
