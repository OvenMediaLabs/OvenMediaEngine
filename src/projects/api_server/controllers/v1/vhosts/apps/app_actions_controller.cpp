//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2020 AirenSoft. All rights reserved.
//
//==============================================================================
#include "app_actions_controller.h"

#include <orchestrator/orchestrator.h>

#include <functional>

#include "../../../../api_private.h"
#include "publishers/publishers.h"

namespace api
{
	namespace v1
	{
		void AppActionsController::PrepareHandlers()
		{
			RegisterGet(R"()", &AppActionsController::OnGetDummyAction);

			//----------------------------------------
			// Record related actions
			//----------------------------------------
			RegisterPost(R"((records))", &AppActionsController::OnPostRecords);
			RegisterPost(R"((startRecord))", &AppActionsController::OnPostStartRecord);
			RegisterPost(R"((stopRecord))", &AppActionsController::OnPostStopRecord);
			// @GET action will be deprecated
			RegisterGet(R"((records))", &AppActionsController::OnGetRecords);

			//----------------------------------------
			// Push related actions
			//----------------------------------------
			RegisterPost(R"((pushes))", &AppActionsController::OnPostPushes);
			RegisterPost(R"((startPush))", &AppActionsController::OnPostStartPush);
			RegisterPost(R"((stopPush))", &AppActionsController::OnPostStopPush);
			// @GET action will be deprecated
			RegisterGet(R"((pushes))", &AppActionsController::OnGetPushes);
		};

		ApiResponse AppActionsController::OnGetRecords(const std::shared_ptr<http::svr::HttpExchange> &client,
													   const std::shared_ptr<mon::HostMetrics> &vhost,
													   const std::shared_ptr<mon::ApplicationMetrics> &app)
		{
			Json::Value empty_body;

			return OnPostRecords(client, empty_body, vhost, app);
		}

		ApiResponse AppActionsController::OnPostRecords(const std::shared_ptr<http::svr::HttpExchange> &client,
														const Json::Value &request_body,
														const std::shared_ptr<mon::HostMetrics> &vhost,
														const std::shared_ptr<mon::ApplicationMetrics> &app)
		{
			Json::Value response;

			auto publisher = std::dynamic_pointer_cast<pub::FilePublisher>(ocst::Orchestrator::GetInstance()->GetPublisherFromType(PublisherType::File));
			if (publisher == nullptr)
			{
				throw http::HttpError(http::StatusCode::NotFound,
									  "Could not find publisher: [%s/%s]",
									  vhost->GetName().CStr(),
									  app->GetVHostAppName().GetAppName().CStr());
			}

			auto application = std::static_pointer_cast<pub::FileApplication>(publisher->GetApplicationByName(app->GetVHostAppName()));
			if (application == nullptr)
			{
				throw http::HttpError(http::StatusCode::NotFound,
									  "Could not find application: [%s/%s]",
									  vhost->GetName().CStr(),
									  app->GetVHostAppName().GetAppName().CStr());
			}

			auto record = ::serdes::RecordFromJson(request_body);
			if (record == nullptr)
			{
				throw http::HttpError(http::StatusCode::BadRequest,
									  "Could not parse json context: [%s/%s]",
									  vhost->GetName().CStr(),
									  app->GetVHostAppName().GetAppName().CStr());
			}
			else
			{
				record->SetVhost(vhost->GetName());
				record->SetApplication(app->GetVHostAppName().GetAppName());
			}

			std::vector<std::shared_ptr<info::Record>> results;

			auto error = application->GetRecords(record, results);
			if (error->GetCode() != pub::FilePublisher::FilePublisherStatusCode::Success)
			{
				throw http::HttpError(http::StatusCode::InternalServerError, "Could not get records: [%s/%s]",
									  vhost->GetName().CStr(),
									  app->GetVHostAppName().GetAppName().CStr());
			}

			response = Json::arrayValue;
			for (auto &item : results)
			{
				response.append(::serdes::JsonFromRecord(item));
			}

			return response;
		}

		ApiResponse AppActionsController::OnPostStartRecord(const std::shared_ptr<http::svr::HttpExchange> &client,
															const Json::Value &request_body,
															const std::shared_ptr<mon::HostMetrics> &vhost,
															const std::shared_ptr<mon::ApplicationMetrics> &app)
		{
			Json::Value response;

			auto publisher = std::dynamic_pointer_cast<pub::FilePublisher>(ocst::Orchestrator::GetInstance()->GetPublisherFromType(PublisherType::File));
			if (publisher == nullptr)
			{
				throw http::HttpError(http::StatusCode::NotFound,
									  "Could not find publisher: [%s/%s]",
									  vhost->GetName().CStr(),
									  app->GetVHostAppName().GetAppName().CStr());
			}

			auto application = std::static_pointer_cast<pub::FileApplication>(publisher->GetApplicationByName(app->GetVHostAppName()));
			if (application == nullptr)
			{
				throw http::HttpError(http::StatusCode::NotFound,
									  "Could not find application: [%s/%s]",
									  vhost->GetName().CStr(),
									  app->GetVHostAppName().GetAppName().CStr());
			}

			auto record = ::serdes::RecordFromJson(request_body);
			if (record == nullptr)
			{
				throw http::HttpError(http::StatusCode::BadRequest,
									  "Could not parse json context: [%s/%s]",
									  vhost->GetName().CStr(),
									  app->GetVHostAppName().GetAppName().CStr());
			}
			else
			{
				record->SetVhost(vhost->GetName());
				record->SetApplication(app->GetVHostAppName().GetAppName());
			}

			// This is a recording task requested via the REST API,
			// and it will remain active until a STOP command is issued through the REST API.
			record->SetByConfig(false);

			auto error = application->RecordStart(record);
			if (error->GetCode() == pub::FilePublisher::FilePublisherStatusCode::FailureInvalidParameter)
			{
				throw http::HttpError(http::StatusCode::BadRequest,
									  error->GetMessage());
			}
			else if (error->GetCode() == pub::FilePublisher::FilePublisherStatusCode::FailureDuplicateKey)
			{
				throw http::HttpError(http::StatusCode::BadRequest,
									  error->GetMessage());
			}

			response = ::serdes::JsonFromRecord(record);

			return {http::StatusCode::OK, std::move(response)};
		}

		ApiResponse AppActionsController::OnPostStopRecord(const std::shared_ptr<http::svr::HttpExchange> &client,
														   const Json::Value &request_body,
														   const std::shared_ptr<mon::HostMetrics> &vhost,
														   const std::shared_ptr<mon::ApplicationMetrics> &app)
		{
			Json::Value response;

			auto publisher = std::dynamic_pointer_cast<pub::FilePublisher>(ocst::Orchestrator::GetInstance()->GetPublisherFromType(PublisherType::File));
			if (publisher == nullptr)
			{
				throw http::HttpError(http::StatusCode::NotFound,
									  "Could not find publisher: [%s/%s]",
									  vhost->GetName().CStr(),
									  app->GetVHostAppName().GetAppName().CStr());
			}

			auto application = std::static_pointer_cast<pub::FileApplication>(publisher->GetApplicationByName(app->GetVHostAppName()));
			if (application == nullptr)
			{
				throw http::HttpError(http::StatusCode::NotFound,
									  "Could not find application: [%s/%s]",
									  vhost->GetName().CStr(),
									  app->GetVHostAppName().GetAppName().CStr());
			}

			auto record = ::serdes::RecordFromJson(request_body);
			if (record == nullptr)
			{
				throw http::HttpError(http::StatusCode::BadRequest,
									  "Could not parse json context: [%s/%s]",
									  vhost->GetName().CStr(),
									  app->GetVHostAppName().GetAppName().CStr());
			}
			else
			{
				record->SetVhost(vhost->GetName());
				record->SetApplication(app->GetVHostAppName().GetAppName());
			}

			auto error = application->RecordStop(record);
			if (error->GetCode() == pub::FilePublisher::FilePublisherStatusCode::FailureInvalidParameter)
			{
				throw http::HttpError(http::StatusCode::BadRequest, error->GetMessage());
			}
			else if (error->GetCode() == pub::FilePublisher::FilePublisherStatusCode::FailureNotExist)
			{
				throw http::HttpError(http::StatusCode::NotFound, error->GetMessage());
			}

			response = ::serdes::JsonFromRecord(record);

			return {http::StatusCode::OK, std::move(response)};
		}

		ApiResponse AppActionsController::OnGetPushes(const std::shared_ptr<http::svr::HttpExchange> &client,
													  const std::shared_ptr<mon::HostMetrics> &vhost,
													  const std::shared_ptr<mon::ApplicationMetrics> &app)
		{
			Json::Value empty_body;

			return OnPostPushes(client, empty_body, vhost, app);
		}

		ApiResponse AppActionsController::OnPostPushes(const std::shared_ptr<http::svr::HttpExchange> &client,
													   const Json::Value &request_body,
													   const std::shared_ptr<mon::HostMetrics> &vhost,
													   const std::shared_ptr<mon::ApplicationMetrics> &app)
		{
			std::vector<std::shared_ptr<info::Push>> results;
			Json::Value response;

			auto push{::serdes::PushFromJson(request_body)};
			if (push == nullptr)
			{
				throw http::HttpError(http::StatusCode::BadRequest,
									  "Could not parse json context: [%s/%s]",
									  vhost->GetName().CStr(),
									  app->GetVHostAppName().GetAppName().CStr());
			}

			auto publisher{ocst::Orchestrator::GetInstance()->GetPublisherFromType(PublisherType::Push)};
			if (publisher == nullptr)
			{
				throw http::HttpError(http::StatusCode::NotFound,
									  "Could not find publisher: [%s/%s]",
									  vhost->GetName().CStr(),
									  app->GetVHostAppName().GetAppName().CStr());
			}

			auto application{std::static_pointer_cast<pub::PushApplication>(publisher->GetApplicationByName(app->GetVHostAppName()))};
			if (application != nullptr)
			{
				application->GetPushes(push, results);
			}

			response = Json::arrayValue;
			for (auto &item : results)
			{
				response.append(::serdes::JsonFromPush(item));
			}

			return {http::StatusCode::OK, std::move(response)};
		}

		ApiResponse AppActionsController::OnPostStartPush(const std::shared_ptr<http::svr::HttpExchange> &client,
														  const Json::Value &request_body,
														  const std::shared_ptr<mon::HostMetrics> &vhost,
														  const std::shared_ptr<mon::ApplicationMetrics> &app)
		{
			Json::Value response;

			auto push{::serdes::PushFromJson(request_body)};
			if (push == nullptr)
			{
				throw http::HttpError(http::StatusCode::BadRequest,
									  "Could not parse json context: [%s/%s]",
									  vhost->GetName().CStr(),
									  app->GetVHostAppName().GetAppName().CStr());
			}

			auto push_protocol{push->GetProtocol().LowerCaseString()};
			auto publisher_type{(push_protocol == "rtmp" || push_protocol == "mpegts" || push_protocol == "srt") ? PublisherType::Push : PublisherType::Unknown};

			if (publisher_type == PublisherType::Unknown)
			{
				throw http::HttpError(http::StatusCode::BadRequest,
									  "Could not find protocol '%s': [%s/%s]",
									  push_protocol.CStr(),
									  vhost->GetName().CStr(),
									  app->GetVHostAppName().GetAppName().CStr());
			}

			auto publisher{ocst::Orchestrator::GetInstance()->GetPublisherFromType(publisher_type)};
			if (publisher == nullptr)
			{
				throw http::HttpError(http::StatusCode::NotFound,
									  "Could not find publisher: [%s/%s]",
									  vhost->GetName().CStr(),
									  app->GetVHostAppName().GetAppName().CStr());
			}

			auto application{std::static_pointer_cast<pub::PushApplication>(publisher->GetApplicationByName(app->GetVHostAppName()))};
			if (application == nullptr)
			{
				throw http::HttpError(http::StatusCode::NotFound,
									  "Could not find application: [%s/%s]",
									  vhost->GetName().CStr(),
									  app->GetVHostAppName().GetAppName().CStr());
			}

			push->SetVhost(vhost->GetName());
			push->SetApplication(app->GetVHostAppName().GetAppName());

			auto error{application->StartPush(push)};
			if (error->GetCode() != pub::PushApplication::ErrorCode::Success)
			{
				throw http::HttpError(http::StatusCode::BadRequest, error->GetMessage());
			}

			// If the stream does not exist, request a pull stream.
			if (application->GetStream(push->GetStreamName()) == nullptr)
			{
				auto url		 = ov::Url::Parse(client->GetRequest()->GetUri());
				auto app_name	 = app->GetVHostAppName();
				auto stream_name = push->GetStreamName();
				ocst::Orchestrator::GetInstance()->RequestPullStreamWithOriginMap(url, app_name, stream_name);
			}

			response = ::serdes::JsonFromPush(push);

			return {http::StatusCode::OK, std::move(response)};
		}

		ApiResponse AppActionsController::OnPostStopPush(const std::shared_ptr<http::svr::HttpExchange> &client,
														 const Json::Value &request_body,
														 const std::shared_ptr<mon::HostMetrics> &vhost,
														 const std::shared_ptr<mon::ApplicationMetrics> &app)
		{
			auto push{::serdes::PushFromJson(request_body)};
			if (push == nullptr)
			{
				throw http::HttpError(http::StatusCode::BadRequest,
									  "Could not parse json context: [%s/%s]",
									  vhost->GetName().CStr(),
									  app->GetVHostAppName().GetAppName().CStr());
			}

			push->SetVhost(vhost->GetName());
			push->SetApplication(app->GetVHostAppName().GetAppName());

			auto publisher{ocst::Orchestrator::GetInstance()->GetPublisherFromType(PublisherType::Push)};
			if (publisher == nullptr)
			{
				throw http::HttpError(http::StatusCode::NotFound,
									  "Could not find publisher: [%s/%s]",
									  vhost->GetName().CStr(),
									  app->GetVHostAppName().GetAppName().CStr());
			}

			auto application{std::static_pointer_cast<pub::PushApplication>(publisher->GetApplicationByName(app->GetVHostAppName()))};
			if (application != nullptr)
			{
				auto error{application->StopPush(push)};
				switch (error->GetCode())
				{
					case pub::PushApplication::ErrorCode::FailureInvalidParameter: {
						throw http::HttpError(http::StatusCode::BadRequest, error->GetMessage());
					}
					case pub::PushApplication::ErrorCode::Success: {
						return {http::StatusCode::OK};
					}
				}
			}

			throw http::HttpError(http::StatusCode::NotFound,
								  "Could not find id '%s': [%s/%s]",
								  push->GetId().CStr(),
								  vhost->GetName().CStr(),
								  app->GetVHostAppName().GetAppName().CStr());
		}

		ApiResponse AppActionsController::OnGetDummyAction(const std::shared_ptr<http::svr::HttpExchange> &client,
														   const std::shared_ptr<mon::HostMetrics> &vhost,
														   const std::shared_ptr<mon::ApplicationMetrics> &app)
		{
			logte("Called OnGetDummyAction. invoke [%s/%s]", vhost->GetName().CStr(), app->GetVHostAppName().GetAppName().CStr());

			return app->GetConfig().ToJson();
		}

	}  // namespace v1
}  // namespace api
