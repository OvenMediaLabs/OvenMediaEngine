//
// Created by getroot on 21. 7. 13.
//

#pragma once

#include <base/ovlibrary/ovlibrary.h>

#include "base/info/host.h"
#include "base/info/managed_queue.h"
#include "host_metrics.h"
#include "queue_metrics.h"

namespace mon
{
	class ServerMetrics : public CommonMetrics
	{
	public:
		ServerMetrics(const std::shared_ptr<const cfg::Server> &server_config);
		void Release();

		void ShowInfo(bool show_children = true) override;

		bool OnHostCreated(const info::Host &host_info);
		bool OnHostDeleted(const info::Host &host_info);

		std::shared_ptr<const cfg::Server> GetConfig();
		std::chrono::system_clock::time_point GetServerStartedTime();
		std::map<uint32_t, std::shared_ptr<HostMetrics>> GetHostMetricsList();
		std::shared_ptr<HostMetrics> GetHostMetrics(info::host_id_t host_id);
		std::shared_ptr<HostMetrics> GetHostMetrics(const info::Host &host_info)
		{
			return GetHostMetrics(host_info.GetId());
		}

	protected:
		std::shared_ptr<const cfg::Server> _server_config = nullptr;
		std::chrono::system_clock::time_point _server_started_time;
		std::shared_mutex _map_guard;
		std::map<uint32_t, std::shared_ptr<HostMetrics>> _hosts;

		// Queue metrics
	public:
		bool OnQueueCreated(const info::ManagedQueue &queue_info);
		bool OnQueueDeleted(const info::ManagedQueue &queue_info);
		bool OnQueueUpdated(const info::ManagedQueue &queue_info, bool with_metadata = false);
		std::map<uint32_t, std::shared_ptr<QueueMetrics>> GetQueueMetricsList();
		std::shared_ptr<mon::QueueMetrics> GetQueueMetrics(const info::ManagedQueue &queue_info);

	protected:
		std::shared_mutex _queue_map_guard;
		std::map<uint32_t, std::shared_ptr<QueueMetrics>> _queues;
	};
}  // namespace mon