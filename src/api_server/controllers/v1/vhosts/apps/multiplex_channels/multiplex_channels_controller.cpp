//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2024 AirenSoft. All rights reserved.
//
//==============================================================================
#include "multiplex_channels_controller.h"

#include <base/ovlibrary/files.h>
#include <orchestrator/orchestrator.h>
#include <providers/multiplex/multiplex_application.h>
#include <providers/multiplex/multiplex_profile.h>

#include "../../../../../api_private.h"

namespace api
{
	namespace v1
	{
		void MultiplexChannelsController::PrepareHandlers()
		{
			RegisterGet(R"()", &MultiplexChannelsController::OnGetChannelList);
			RegisterPost(R"()", &MultiplexChannelsController::OnCreateChannel);
			RegisterGet(R"(\/(?<stream_name>[^\/]*))", &MultiplexChannelsController::OnGetChannel);
			RegisterDelete(R"(\/(?<stream_name>[^\/]*))", &MultiplexChannelsController::OnDeleteChannel);
		};

		ApiResponse MultiplexChannelsController::OnGetChannelList(const std::shared_ptr<http::svr::HttpExchange> &client,
																  const std::shared_ptr<mon::HostMetrics> &vhost,
																  const std::shared_ptr<mon::ApplicationMetrics> &app)
		{
			Json::Value response = Json::arrayValue;

			// Get Real MultiplexStream
			auto provider		 = std::dynamic_pointer_cast<pvd::Provider>(ocst::Orchestrator::GetInstance()->GetProviderFromType(ProviderType::Multiplex));
			if (provider == nullptr)
			{
				throw http::HttpError(http::StatusCode::InternalServerError, "Could not get the multiplex provider");
			}

			auto multiplex_application = std::static_pointer_cast<pvd::MultiplexApplication>(provider->GetApplicationByName(app->GetVHostAppName()));
			if (multiplex_application == nullptr)
			{
				throw http::HttpError(http::StatusCode::NotFound, "Could not get the multiplex application");
			}

			for (auto &item : multiplex_application->GetMultiplexStreams())
			{
				auto &stream = item.second;

				if (stream->GetSourceType() != StreamSourceType::Multiplex)
				{
					continue;
				}

				if (stream->GetLinkedInputStream() == nullptr)
				{
					response.append(stream->GetName().CStr());
				}
			}

			return response;
		}

		ApiResponse MultiplexChannelsController::OnCreateChannel(const std::shared_ptr<http::svr::HttpExchange> &client, const Json::Value &request_body,
																 const std::shared_ptr<mon::HostMetrics> &vhost,
																 const std::shared_ptr<mon::ApplicationMetrics> &app)
		{
			// Validation check
			if (!request_body.isObject())
			{
				throw http::HttpError(http::StatusCode::BadRequest, "Invalid request body");
			}

			// Get Config
			auto multiplex_config = app->GetConfig().GetProviders().GetMultiplexProvider();
			if (multiplex_config.IsParsed() == false)
			{
				return {http::StatusCode::Forbidden};  // Not enabled
			}

			auto multiplex_files_dir = ov::GetDirPath(multiplex_config.GetMuxFilesDir(), cfg::ConfigManager::GetInstance()->GetConfigPath());

			auto [multiplex, error]	 = pvd::MultiplexProfile::CreateFromJsonObject(request_body);
			if (multiplex == nullptr)
			{
				throw http::HttpError(http::StatusCode::BadRequest, error);
			}

			// Check if stream is already exists
			auto stream_name = multiplex->GetOutputStreamName();
			auto stream		 = GetStream(app, stream_name, nullptr);
			if (stream != nullptr)
			{
				throw http::HttpError(http::StatusCode::Conflict, "Stream '%s' already exists", stream_name.CStr());
			}

			auto multiplex_file_path = ov::String::FormatString("%s/%s.%s", multiplex_files_dir.CStr(), multiplex->GetOutputStreamName().CStr(), pvd::MultiplexFileExtension);

			auto error_code			 = multiplex->SaveToXMLFile(multiplex_file_path);
			if (error_code != CommonErrorCode::SUCCESS)
			{
				throw http::HttpError(http::StatusCode::InternalServerError, "Could not save the multiplex file %s", multiplex_file_path.CStr());
			}

			return {http::StatusCode::Created};
		}

		ApiResponse MultiplexChannelsController::OnGetChannel(const std::shared_ptr<http::svr::HttpExchange> &client,
															  const std::shared_ptr<mon::HostMetrics> &vhost,
															  const std::shared_ptr<mon::ApplicationMetrics> &app)
		{
			const auto &match_result = client->GetRequest()->GetMatchResult();
			const auto stream_name	 = match_result.GetNamedGroup("stream_name").GetValue();

			if (stream_name.IsEmpty())
			{
				throw http::HttpError(http::StatusCode::BadRequest, "Invalid stream name");
			}

			// Get Real MultiplexStream
			auto provider = std::dynamic_pointer_cast<pvd::Provider>(ocst::Orchestrator::GetInstance()->GetProviderFromType(ProviderType::Multiplex));
			if (provider == nullptr)
			{
				throw http::HttpError(http::StatusCode::InternalServerError, "Could not get the multiplex provider");
			}

			auto multiplex_application = std::static_pointer_cast<pvd::MultiplexApplication>(provider->GetApplicationByName(app->GetVHostAppName()));
			if (multiplex_application == nullptr)
			{
				throw http::HttpError(http::StatusCode::NotFound, "Could not get the multiplex application");
			}

			auto multiplex_stream = multiplex_application->GetMultiplexStream(stream_name);
			if (multiplex_stream == nullptr)
			{
				throw http::HttpError(http::StatusCode::NotFound, "Could not get the multiplex stream <%s>", stream_name.CStr());
			}

			auto multiplex = multiplex_stream->GetProfile();
			if (multiplex == nullptr)
			{
				throw http::HttpError(http::StatusCode::NotFound, "Could not get the multiplex");
			}

			Json::Value response;
			if (multiplex->ToJsonObject(response) != CommonErrorCode::SUCCESS)
			{
				throw http::HttpError(http::StatusCode::InternalServerError, "Could not convert the multiplex to json");
			}

			response["state"] = multiplex_stream->GetMuxStateStr().CStr();
			if (multiplex_stream->GetMuxState() == pvd::MultiplexStream::MuxState::Pulling)
			{
				response["pullingMessage"] = multiplex_stream->GetPullingStateMsg().CStr();
			}

			return response;
		}

		ApiResponse MultiplexChannelsController::OnDeleteChannel(const std::shared_ptr<http::svr::HttpExchange> &client,
																 const std::shared_ptr<mon::HostMetrics> &vhost,
																 const std::shared_ptr<mon::ApplicationMetrics> &app)
		{
			const auto &match_result = client->GetRequest()->GetMatchResult();
			const auto stream_name	 = match_result.GetNamedGroup("stream_name").GetValue();

			if (stream_name.IsEmpty())
			{
				throw http::HttpError(http::StatusCode::BadRequest, "Invalid stream name");
			}

			// Get Real MultiplexStream
			auto provider = std::dynamic_pointer_cast<pvd::Provider>(ocst::Orchestrator::GetInstance()->GetProviderFromType(ProviderType::Multiplex));
			if (provider == nullptr)
			{
				throw http::HttpError(http::StatusCode::InternalServerError, "Could not get the multiplex provider");
			}

			auto multiplex_application = std::static_pointer_cast<pvd::MultiplexApplication>(provider->GetApplicationByName(app->GetVHostAppName()));
			if (multiplex_application == nullptr)
			{
				throw http::HttpError(http::StatusCode::NotFound, "Could not get the multiplex application");
			}

			auto multiplex_stream = multiplex_application->GetMultiplexStream(stream_name);
			if (multiplex_stream == nullptr)
			{
				throw http::HttpError(http::StatusCode::NotFound, "Could not get the multiplex stream <%s>", stream_name.CStr());
			}

			// Get Config
			auto multiplex_config = app->GetConfig().GetProviders().GetMultiplexProvider();
			if (multiplex_config.IsParsed() == false)
			{
				return {http::StatusCode::Forbidden};  // Not enabled
			}

			auto multiplex_files_dir = ov::GetDirPath(multiplex_config.GetMuxFilesDir(), cfg::ConfigManager::GetInstance()->GetConfigPath());
			auto multiplex_file_path = ov::String::FormatString("%s/%s.%s", multiplex_files_dir.CStr(), stream_name.CStr(), pvd::MultiplexFileExtension);

			// Delete multiplex file
			if (ov::DeleteFile(multiplex_file_path) == false)
			{
				throw http::HttpError(http::StatusCode::InternalServerError, "Could not delete the multiplex file %s", multiplex_file_path.CStr());
			}

			return {http::StatusCode::OK};
		}
	}  // namespace v1
}  // namespace api
