//
// Created by getroot on 20. 1. 16.
//

#pragma once

#include "./alert/alert.h"
#include "base/info/info.h"
#include "base/ovlibrary/delay_queue.h"
#include "event_forwarder.h"
#include "event_logger.h"
#include "server_metrics.h"

#define MonitorInstance mon::Monitoring::GetInstance()
#define HostMetrics(info) mon::Monitoring::GetInstance()->GetHostMetrics(info);
#define ApplicationMetrics(info) mon::Monitoring::GetInstance()->GetApplicationMetrics(info);
#define StreamMetrics(info) mon::Monitoring::GetInstance()->GetStreamMetrics(info);

namespace mon
{
	class Monitoring
	{
	public:
		static Monitoring *GetInstance()
		{
			static Monitoring monitor;
			return &monitor;
		}

		void Release();

		void SetLogPath(const ov::String &log_path);

		bool IsAnalyticsOn()
		{
			return _is_analytics_on;
		}

		std::shared_ptr<ServerMetrics> GetServerMetrics();
		std::map<uint32_t, std::shared_ptr<HostMetrics>> GetHostMetricsList();
		std::shared_ptr<HostMetrics> GetHostMetrics(const info::Host &host_info);
		std::shared_ptr<ApplicationMetrics> GetApplicationMetrics(const info::Application &app_info);
		std::shared_ptr<StreamMetrics> GetStreamMetrics(const info::Stream &stream_info);

		// Events
		void OnServerStarted(const std::shared_ptr<const cfg::Server> &server_config);
		bool OnHostCreated(const info::Host &host_info);
		bool OnHostDeleted(const info::Host &host_info);
		bool OnApplicationCreated(const info::Application &app_info);
		bool OnApplicationDeleted(const info::Application &app_info);
		bool OnStreamCreated(const info::Stream &stream_info);
		bool OnStreamCreationFailed(const info::Stream &stream_info);
		bool OnStreamPrepared(const info::Stream &stream_info);
		bool OnStreamDeleted(const info::Stream &stream_info);
		bool OnStreamUpdated(const info::Stream &stream_info);

		void IncreaseBytesIn(const info::Stream &stream_info, uint64_t value);
		void IncreaseBytesOut(const info::Stream &stream_info, PublisherType type, uint64_t value);
		void OnSessionConnected(const info::Stream &stream_info, PublisherType type);
		void OnSessionDisconnected(const info::Stream &stream_info, PublisherType type);
		void OnSessionsDisconnected(const info::Stream &stream_info, PublisherType type, uint64_t number_of_sessions);

		std::shared_ptr<alrt::Alert> GetAlert();

	private:
		ov::DelayQueue _timer{"MonLogTimer"};
		std::shared_ptr<ServerMetrics> _server_metric = nullptr;
		EventLogger _logger;
		EventForwarder _forwarder;
		std::shared_ptr<alrt::Alert> _alert = nullptr;
		bool _is_analytics_on				= false;
	};
}  // namespace mon