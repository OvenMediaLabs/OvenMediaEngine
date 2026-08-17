//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2020 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <monitoring/monitoring.h>
#include <orchestrator/orchestrator.h>
#include <modules/http/server/http_exchange.h>
#include "./multiple_status.h"

namespace api
{
	std::map<uint32_t, std::shared_ptr<mon::HostMetrics>> GetVirtualHostList();
	std::shared_ptr<mon::HostMetrics> GetVirtualHost(const ov::String &vhost_name);

	std::map<uint32_t, std::shared_ptr<mon::ApplicationMetrics>> GetApplicationList(const std::shared_ptr<mon::HostMetrics> &vhost);
	std::shared_ptr<mon::ApplicationMetrics> GetApplication(const std::shared_ptr<mon::HostMetrics> &vhost, const ov::String &app_name);

	std::map<uint32_t, std::shared_ptr<mon::StreamMetrics>> GetStreamList(const std::shared_ptr<mon::ApplicationMetrics> &application);
	std::shared_ptr<mon::StreamMetrics> GetStream(const std::shared_ptr<mon::ApplicationMetrics> &application, const ov::String &stream_name, std::vector<std::shared_ptr<mon::StreamMetrics>> *output_streams);

	MAY_THROWS(http::HttpError)
	void GetRequestBody(
		const std::shared_ptr<http::svr::HttpExchange> &client,
		Json::Value *request_body);

	MAY_THROWS(http::HttpError)
	void GetVirtualHostMetrics(
		const ov::MatchResult &match_result,
		std::shared_ptr<mon::HostMetrics> *vhost_metrics);

	MAY_THROWS(http::HttpError)
	void GetApplicationMetrics(
		const ov::MatchResult &match_result,
		const std::shared_ptr<mon::HostMetrics> &vhost_metrics,
		std::shared_ptr<mon::ApplicationMetrics> *app_metrics);

	MAY_THROWS(http::HttpError)
	void GetStreamMetrics(
		const ov::MatchResult &match_result,
		const std::shared_ptr<mon::HostMetrics> &vhost_metrics,
		const std::shared_ptr<mon::ApplicationMetrics> &app_metrics,
		std::shared_ptr<mon::StreamMetrics> *stream_metrics,
		std::vector<std::shared_ptr<mon::StreamMetrics>> *output_streams);

	void FillDefaultAppConfigValues(Json::Value &app_config);

	void OverwriteJson(const Json::Value &from, Json::Value &to);

	MAY_THROWS(http::HttpError)
	void ThrowIfVirtualIsReadOnly(const cfg::vhost::VirtualHost &vhost_config);
	MAY_THROWS(http::HttpError)
	void ThrowIfOrchestratorNotSucceeded(ocst::Result result, const char *action, const char *resource_name, const char *resource_path);

	MAY_THROWS(http::HttpError)
	void RecreateApplication(const std::shared_ptr<mon::HostMetrics> &vhost,
							 const std::shared_ptr<mon::ApplicationMetrics> &app,
							 Json::Value &app_json);

	ov::String GetOutputProfileName(const std::shared_ptr<http::svr::HttpExchange> &client);

	off_t FindOutputProfile(const std::shared_ptr<mon::ApplicationMetrics> &app,
							const ov::String &output_profile_name,
							Json::Value *value);

	off_t FindOutputProfile(Json::Value &app_json,
							const ov::String &output_profile_name,
							Json::Value **value);
}  // namespace api
