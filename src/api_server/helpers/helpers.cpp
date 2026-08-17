//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2020 AirenSoft. All rights reserved.
//
//==============================================================================
#include "./helpers.h"

#include <modules/http/http.h>
#include <modules/json_serdes/converters.h>

namespace api
{
	std::map<uint32_t, std::shared_ptr<mon::HostMetrics>> GetVirtualHostList()
	{
		return mon::Monitoring::GetInstance()->GetHostMetricsList();
	}

	std::shared_ptr<mon::HostMetrics> GetVirtualHost(const ov::String &vhost_name)
	{
		auto vhost_list = GetVirtualHostList();

		for (const auto &vhost : vhost_list)
		{
			if (vhost_name == vhost.second->GetName().CStr())
			{
				return vhost.second;
			}
		}

		return nullptr;
	}

	std::map<uint32_t, std::shared_ptr<mon::ApplicationMetrics>> GetApplicationList(const std::shared_ptr<mon::HostMetrics> &vhost)
	{
		return vhost->GetApplicationMetricsList();
	}

	std::shared_ptr<mon::ApplicationMetrics> GetApplication(const std::shared_ptr<mon::HostMetrics> &vhost, const ov::String &app_name)
	{
		if (vhost != nullptr)
		{
			auto app_list = GetApplicationList(vhost);
			for (auto &item : app_list)
			{
				if (app_name == item.second->GetVHostAppName().GetAppName().CStr())
				{
					return item.second;
				}
			}
		}

		return nullptr;
	}

	std::map<uint32_t, std::shared_ptr<mon::StreamMetrics>> GetStreamList(const std::shared_ptr<mon::ApplicationMetrics> &application)
	{
		return application->GetStreamMetricsMap();
	}

	std::shared_ptr<mon::StreamMetrics> GetStream(const std::shared_ptr<mon::ApplicationMetrics> &application, const ov::String &stream_name, std::vector<std::shared_ptr<mon::StreamMetrics>> *output_streams)
	{
		if (application != nullptr)
		{
			auto stream_list = GetStreamList(application);

			std::shared_ptr<mon::StreamMetrics> input_stream;

			for (auto &item : stream_list)
			{
				auto &stream = item.second;

				if (input_stream == nullptr)
				{
					if (
						// Stream is an input stream
						(stream->GetLinkedInputStream() == nullptr) &&
						(stream_name == stream->GetName().CStr()))
					{
						input_stream = item.second;

						if (output_streams == nullptr)
						{
							break;
						}
						else
						{
							// Continue to find the stream associated with input_stream
						}
					}
				}
				else
				{
					auto &origin_stream = stream->GetLinkedInputStream();

					if ((origin_stream != nullptr) && (origin_stream->GetId() == input_stream->GetId()))
					{
						output_streams->push_back(item.second);
					}
				}
			}

			return input_stream;
		}

		return nullptr;
	}

	void GetRequestBody(
		const std::shared_ptr<http::svr::HttpExchange> &client,
		Json::Value *request_body)
	{
		ov::JsonObject json_object;
		auto error = json_object.Parse(client->GetRequest()->GetRequestBody());

		if (error != nullptr)
		{
			throw http::HttpError(http::StatusCode::BadRequest, error->What());
		}

		if (request_body != nullptr)
		{
			*request_body = json_object.GetJsonValue();
		}
	}

	void GetVirtualHostMetrics(
		const ov::MatchResult &match_result,
		std::shared_ptr<mon::HostMetrics> *vhost_metrics)
	{
		auto vhost_name_group = match_result.GetNamedGroup("vhost_name");

		if (vhost_name_group.IsValid() == false)
		{
			throw http::HttpError(
				http::StatusCode::InternalServerError,
				"Could not find the virtual host regex group");
		}

		auto vhost_name = vhost_name_group.GetValue();
		auto vhost = GetVirtualHost(vhost_name);

		if (vhost == nullptr)
		{
			throw http::HttpError(
				http::StatusCode::NotFound,
				"Could not find the virtual host: [%s]", vhost_name.CStr());
		}

		if (vhost_metrics != nullptr)
		{
			*vhost_metrics = vhost;
		}
	}

	void GetApplicationMetrics(
		const ov::MatchResult &match_result,
		const std::shared_ptr<mon::HostMetrics> &vhost_metrics,
		std::shared_ptr<mon::ApplicationMetrics> *app_metrics)
	{
		auto app_name_group = match_result.GetNamedGroup("app_name");

		if (app_name_group.IsValid() == false)
		{
			throw http::HttpError(
				http::StatusCode::InternalServerError,
				"Could not find the application regex group");
		}

		auto vhost_name = vhost_metrics->GetName();
		auto app_name = app_name_group.GetValue();
		auto app = GetApplication(vhost_metrics, app_name);

		if (app == nullptr)
		{
			throw http::HttpError(
				http::StatusCode::NotFound,
				"Could not find the application: [%s/%s]", vhost_name.CStr(), app_name.CStr());
		}

		if (app_metrics != nullptr)
		{
			*app_metrics = app;
		}
	}

	MAY_THROWS(http::HttpError)
	void GetStreamMetrics(
		const ov::MatchResult &match_result,
		const std::shared_ptr<mon::HostMetrics> &vhost_metrics,
		const std::shared_ptr<mon::ApplicationMetrics> &app_metrics,
		std::shared_ptr<mon::StreamMetrics> *stream_metrics,
		std::vector<std::shared_ptr<mon::StreamMetrics>> *output_streams)
	{
		auto stream_name_group = match_result.GetNamedGroup("stream_name");

		if (stream_name_group.IsValid() == false)
		{
			throw http::HttpError(
				http::StatusCode::InternalServerError,
				"Could not find the stream regex group");
		}

		auto vhost_name = vhost_metrics->GetName();
		auto app_name = app_metrics->GetVHostAppName();
		auto stream_name = stream_name_group.GetValue();
		auto stream = GetStream(app_metrics, stream_name, output_streams);

		if (stream == nullptr)
		{
			throw http::HttpError(
				http::StatusCode::NotFound,
				"Could not find the stream: [%s/%s/%s]", vhost_name.CStr(), app_name.CStr(), stream_name.CStr());
		}

		if (stream_metrics != nullptr)
		{
			*stream_metrics = stream;
		}
	}

	void FillDefaultAppConfigValues(Json::Value &app_config)
	{
		// Setting up the default values
		if (app_config.isMember("providers") == false)
		{
			auto &providers = app_config["providers"];

			providers["ovt"] = Json::objectValue;
			providers["rtspPull"] = Json::objectValue;

			providers["webrtc"] = Json::objectValue;
			providers["srt"] = Json::objectValue;
			providers["rtmp"] = Json::objectValue;
		}

		if (app_config.isMember("publishers") == false)
		{
			auto &publishers = app_config["publishers"];

			publishers["llhls"] = Json::objectValue;
			publishers["webrtc"] = Json::objectValue;
			publishers["ovt"] = Json::objectValue;
		}

		if (app_config.isMember("outputProfiles") == false)
		{
			Json::Value output_profile(Json::objectValue);

			output_profile["name"] = "default";
			output_profile["outputStreamName"] = "${OriginStreamName}";

			auto &encodes = output_profile["encodes"];

			Json::Value codec;
			codec["name"] = "bypass_video";
			codec["bypass"] = true;

			encodes["videos"].append(codec);

			codec = Json::objectValue;
			codec["name"] = "opus";
			codec["codec"] = "opus";
			codec["bitrate"] = 128000;
			codec["samplerate"] = 48000;
			codec["channel"] = 2;
			codec["bypassIfMatch"]["codec"] = "eq";
			encodes["audios"].append(codec);

			codec = Json::objectValue;
			codec["name"] = "aac";
			codec["codec"] = "aac";
			codec["bitrate"] = 128000;
			codec["samplerate"] = 48000;
			codec["channel"] = 2;
			codec["bypassIfMatch"]["codec"] = "eq";
			encodes["audios"].append(codec);

			app_config["outputProfiles"]["outputProfile"].append(output_profile);
		}
	}

	void OverwriteJson(const Json::Value &from, Json::Value &to)
	{
		for (auto item = from.begin(); item != from.end(); ++item)
		{
			switch (item->type())
			{
				case Json::ValueType::nullValue:
					[[fallthrough]];
				case Json::ValueType::intValue:
					[[fallthrough]];
				case Json::ValueType::uintValue:
					[[fallthrough]];
				case Json::ValueType::realValue:
					[[fallthrough]];
				case Json::ValueType::stringValue:
					[[fallthrough]];
				case Json::ValueType::booleanValue:
					[[fallthrough]];
				case Json::ValueType::arrayValue:
					to[item.name()] = *item;
					break;

				case Json::ValueType::objectValue: {
					OverwriteJson(*item, to[item.name()]);
				}
			}
		}
	}

	void ThrowIfVirtualIsReadOnly(const cfg::vhost::VirtualHost &vhost_config)
	{
		if (vhost_config.IsReadOnly())
		{
			throw http::HttpError(http::StatusCode::Forbidden,
								  "The VirtualHost is read-only: [%s]",
								  vhost_config.GetName().CStr());
		}
	}

	void ThrowIfOrchestratorNotSucceeded(ocst::Result result, const char *action, const char *resource_name, const char *resource_path)
	{
		switch (result)
		{
			case ocst::Result::Failed:
				throw http::HttpError(http::StatusCode::InternalServerError,
									  "Failed to %s the %s: [%s]", action, resource_name, resource_path);

			case ocst::Result::Succeeded:
				break;

			case ocst::Result::Exists:
				throw http::HttpError(http::StatusCode::Conflict,
									  "Could not %s the %s: [%s] already exists", action, resource_name, resource_path);

			case ocst::Result::NotExists:
				throw http::HttpError(http::StatusCode::NotFound,
									  "Could not %s the %s: [%s] not exists", action, resource_name, resource_path);
		}
	}

	void RecreateApplication(const std::shared_ptr<mon::HostMetrics> &vhost,
							 const std::shared_ptr<mon::ApplicationMetrics> &app,
							 Json::Value &app_json)
	{
		ThrowIfVirtualIsReadOnly(*(vhost.get()));

		// TODO(dimiden): Caution - Race condition may occur
		// If an application is deleted immediately after the GetApplication(),
		// the app information can no longer be obtained from Orchestrator

		// Delete GET-only fields
		app_json.removeMember("dynamic");

		cfg::vhost::app::Application app_config;
		try
		{
			::serdes::ApplicationFromJson(app_json, &app_config);
		}
		catch (const cfg::ConfigError &error)
		{
			throw http::HttpError(http::StatusCode::BadRequest, error.What());
		}

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
	}

	ov::String GetOutputProfileName(const std::shared_ptr<http::svr::HttpExchange> &client)
	{
		auto &match_result = client->GetRequest()->GetMatchResult();

		return match_result.GetNamedGroup("output_profile_name").GetValue();
	}

	off_t FindOutputProfile(const std::shared_ptr<mon::ApplicationMetrics> &app,
							const ov::String &output_profile_name,
							Json::Value *value)
	{
		auto &app_config = app->GetConfig();
		off_t offset = 0;

		for (auto &profile : app_config.GetOutputProfileList())
		{
			if (output_profile_name == profile.GetName().CStr())
			{
				if (value != nullptr)
				{
					*value = profile.ToJson();
				}

				return offset;
			}

			offset++;
		}

		return -1;
	}

	off_t FindOutputProfile(Json::Value &app_json,
							const ov::String &output_profile_name,
							Json::Value **value)
	{
		if (app_json.isMember("outputProfiles"))
		{
			auto &output_profiles = app_json["outputProfiles"];

			if (output_profiles.isMember("outputProfile"))
			{
				auto &output_profile_list = output_profiles["outputProfile"];
				off_t offset = 0;

				if (output_profile_list.isArray())
				{
					for (auto &profile : output_profile_list)
					{
						auto name = profile["name"];

						if (name.isString())
						{
							if (output_profile_name == name.asCString())
							{
								if (value != nullptr)
								{
									*value = &profile;
								}

								return offset;
							}
						}
						else
						{
							// Invalid name
							OV_ASSERT(false, "String is expected, but %d found", name.type());
						}

						offset++;
					}
				}
			}
		}

		return -1;
	}
}  // namespace api
