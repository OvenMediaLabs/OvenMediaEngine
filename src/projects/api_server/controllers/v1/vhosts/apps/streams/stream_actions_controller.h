//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2022 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include "../../../../controller_base.h"
#include "publishers/publishers.h"
namespace api
{
	namespace v1
	{
		class StreamActionsController : public ControllerBase<StreamActionsController>
		{
		public:
			void PrepareHandlers() override;

			ApiResponse OnPostHLSDumps(const std::shared_ptr<http::svr::HttpExchange> &client, const Json::Value &request_body,
									   const std::shared_ptr<mon::HostMetrics> &vhost,
									   const std::shared_ptr<mon::ApplicationMetrics> &app,
									   const std::shared_ptr<mon::StreamMetrics> &stream,
									   const std::vector<std::shared_ptr<mon::StreamMetrics>> &output_streams);

			// POST /v1/vhosts/<vhost_name>/apps/<app_name>/streams/<stream_name>:startHlsDumps
			ApiResponse OnPostStartHLSDump(const std::shared_ptr<http::svr::HttpExchange> &client, const Json::Value &request_body,
										   const std::shared_ptr<mon::HostMetrics> &vhost,
										   const std::shared_ptr<mon::ApplicationMetrics> &app,
										   const std::shared_ptr<mon::StreamMetrics> &stream,
										   const std::vector<std::shared_ptr<mon::StreamMetrics>> &output_streams);

			// POST /v1/vhosts/<vhost_name>/apps/<app_name>/streams/<stream_name>:stopHlsDumps
			ApiResponse OnPostStopHLSDump(const std::shared_ptr<http::svr::HttpExchange> &client, const Json::Value &request_body,
										  const std::shared_ptr<mon::HostMetrics> &vhost,
										  const std::shared_ptr<mon::ApplicationMetrics> &app,
										  const std::shared_ptr<mon::StreamMetrics> &stream,
										  const std::vector<std::shared_ptr<mon::StreamMetrics>> &output_streams);

			// POST /v1/vhosts/<vhost_name>/apps/<app_name>/streams/<stream_name>:sendEvent
			ApiResponse OnPostSendEvent(const std::shared_ptr<http::svr::HttpExchange> &client, const Json::Value &request_body,
										const std::shared_ptr<mon::HostMetrics> &vhost,
										const std::shared_ptr<mon::ApplicationMetrics> &app,
										const std::shared_ptr<mon::StreamMetrics> &stream,
										const std::vector<std::shared_ptr<mon::StreamMetrics>> &output_streams);

			// POST /v1/vhosts/<vhost_name>/apps/<app_name>/streams/<stream_name>:sendEvent
			ApiResponse OnPostSendEvents(const std::shared_ptr<http::svr::HttpExchange> &client, const Json::Value &request_body,
										 const std::shared_ptr<mon::HostMetrics> &vhost,
										 const std::shared_ptr<mon::ApplicationMetrics> &app,
										 const std::shared_ptr<mon::StreamMetrics> &stream,
										 const std::vector<std::shared_ptr<mon::StreamMetrics>> &output_streams);

			// POST /v1/vhosts/<vhost_name>/apps/<app_name>/streams/<stream_name>:concludeHlsLive
			ApiResponse OnPostConcludeHlsLive(const std::shared_ptr<http::svr::HttpExchange> &client, const Json::Value &request_body,
											  const std::shared_ptr<mon::HostMetrics> &vhost,
											  const std::shared_ptr<mon::ApplicationMetrics> &app,
											  const std::shared_ptr<mon::StreamMetrics> &stream,
											  const std::vector<std::shared_ptr<mon::StreamMetrics>> &output_streams);

			// POST /v1/vhosts/<vhost_name>/apps/<app_name>/streams/<stream_name>:sendEvent
			ApiResponse OnPostSendSubtitles(const std::shared_ptr<http::svr::HttpExchange> &client, const Json::Value &request_body,
										   const std::shared_ptr<mon::HostMetrics> &vhost,
										   const std::shared_ptr<mon::ApplicationMetrics> &app,
										   const std::shared_ptr<mon::StreamMetrics> &stream,
										   const std::vector<std::shared_ptr<mon::StreamMetrics>> &output_streams);

		private:
			// TODO(Getroot): Move to mon::StreamMetrics
			std::shared_ptr<pvd::Stream> GetSourceStream(const std::shared_ptr<mon::StreamMetrics> &stream);

			// Template
			template <typename T>
			std::shared_ptr<T> GetOutputStream(const std::shared_ptr<mon::StreamMetrics> &stream_metric, PublisherType type)
			{
				auto publisher = std::dynamic_pointer_cast<LLHlsPublisher>(ocst::Orchestrator::GetInstance()->GetPublisherFromType(type));
				if (publisher == nullptr)
				{
					return nullptr;
				}

				auto appliation = publisher->GetApplicationByName(stream_metric->GetApplicationInfo().GetVHostAppName());
				if (appliation == nullptr)
				{
					return nullptr;
				}

				auto stream = appliation->GetStream(stream_metric->GetName());
				if (stream == nullptr)
				{
					return nullptr;
				}

				return std::static_pointer_cast<T>(stream);
			}

			std::shared_ptr<ov::Data> MakeID3Data(const Json::Value &events);						 // ID3v2
			std::shared_ptr<ov::Data> MakeCueData(const Json::Value &events);						 // CUE
			std::shared_ptr<ov::Data> MakeAMFData(const Json::Value &events);						 // AMF
			std::shared_ptr<ov::Data> MakeSEIData(const Json::Value &events);						 // SEI
			std::shared_ptr<ov::Data> MakeScte35Data(const Json::Value &events, int64_t timestamp);	 // SCTE35
		};
	}  // namespace v1
}  // namespace api
