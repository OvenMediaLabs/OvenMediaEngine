//
// Created by getroot on 20. 1. 16.
//

#pragma once

#include "./alert/alert.h"
#include "base/info/info.h"
#include "base/ovlibrary/delay_queue.h"
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

		std::shared_ptr<ServerMetrics> GetServerMetrics();
		std::map<uint32_t, std::shared_ptr<HostMetrics>> GetHostMetricsList();
		std::map<uint32_t, std::shared_ptr<QueueMetrics>> GetQueueMetricsList();
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

		// Queue events (proxy for ServerMetrics)
		bool OnQueueCreated(const info::ManagedQueue &queue_info);
		void OnQueueDeleted(const info::ManagedQueue &queue_info);
		void OnQueueUpdated(const info::ManagedQueue &queue_info, bool with_metadata = false);

		std::shared_ptr<alrt::Alert> GetAlert();
		// Alert proxy methods (thread-safe)
		void SendStreamAlertMessage(alrt::Message::Code code, const std::shared_ptr<StreamMetrics> &stream_metric, const std::shared_ptr<StreamMetrics> &parent_stream_metric, const std::shared_ptr<alrt::ExtraData> &extra);
		void SendStreamAlertMessage(alrt::Message::Code code, const std::shared_ptr<StreamMetrics> &stream_metric);

		

	private:
		ov::DelayQueue _timer{"MonLogTimer"};
		mutable std::shared_mutex _server_metric_guard;
		std::shared_ptr<ServerMetrics> _server_metric = nullptr;
		mutable std::shared_mutex _alert_guard;
		std::shared_ptr<alrt::Alert> _alert = nullptr;
	};
}  // namespace mon