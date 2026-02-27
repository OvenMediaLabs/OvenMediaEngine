#include "server_metrics.h"

#include <malloc.h>
#include <orchestrator/orchestrator.h>

#include "monitoring_private.h"

namespace mon
{
	ServerMetrics::ServerMetrics(const std::shared_ptr<const cfg::Server> &server_config)
		: _server_config(server_config)
	{
		_server_started_time = std::chrono::system_clock::now();
	}

	void ServerMetrics::ShowInfo([[maybe_unused]] bool show_children)
	{
		std::shared_lock<std::shared_mutex> lock(_map_guard);
		for (const auto &t : _hosts)
		{
			auto &host = t.second;
			host->ShowInfo();
		}
	}

	void ServerMetrics::Release()
	{
		std::shared_lock<std::shared_mutex> lock(_map_guard);
		for (const auto &host : _hosts)
		{
			host.second->Release();
		}
	}

	std::chrono::system_clock::time_point ServerMetrics::GetServerStartedTime()
	{
		return _server_started_time;
	}

	std::shared_ptr<const cfg::Server> ServerMetrics::GetConfig()
	{
		return _server_config;
	}

	bool ServerMetrics::OnHostCreated(const info::Host &host_info)
	{
		std::unique_lock<std::shared_mutex> lock(_map_guard);
		if (_hosts.find(host_info.GetId()) != _hosts.end())
		{
			return true;
		}
		auto host_metrics = std::make_shared<HostMetrics>(host_info);
		if (host_metrics == nullptr)
		{
			logte("Cannot create HostMetrics (%s/%s)", host_info.GetName().CStr(), host_info.GetUUID().CStr());
			return false;
		}

		_hosts[host_info.GetId()] = host_metrics;

		logti("Create HostMetrics(%s/%s) for monitoring", host_info.GetName().CStr(), host_info.GetUUID().CStr());

		return true;
	}

	bool ServerMetrics::OnHostDeleted(const info::Host &host_info)
	{
		std::unique_lock<std::shared_mutex> lock(_map_guard);
		auto it = _hosts.find(host_info.GetId());

		if (it == _hosts.end())
		{
			return false;
		}

		auto host = it->second;
		_hosts.erase(it);
		host->Release();

		logti("Delete HostMetrics(%s/%s) for monitoring", host_info.GetName().CStr(), host_info.GetUUID().CStr());

		return true;
	}

	std::map<uint32_t, std::shared_ptr<HostMetrics>> ServerMetrics::GetHostMetricsList()
	{
		std::shared_lock<std::shared_mutex> lock(_map_guard);
		return _hosts;
	}

	std::shared_ptr<HostMetrics> ServerMetrics::GetHostMetrics(info::host_id_t host_id)
	{
		std::shared_lock<std::shared_mutex> lock(_map_guard);
		if (_hosts.find(host_id) == _hosts.end())
		{
			return nullptr;
		}

		return _hosts[host_id];
	}

	bool ServerMetrics::OnQueueCreated(const info::ManagedQueue &queue_info)
	{
		if (GetQueueMetrics(queue_info) != nullptr)
		{
			logtw("Dupulicate QueueMetrics(%u/%s) is created", queue_info.GetId(), queue_info.ToString().CStr());
			return false;
		}

		auto queue_metrics = std::make_shared<QueueMetrics>(queue_info);
		if (queue_metrics == nullptr)
		{
			logte("Cannot create QueueMetrics (%u/%s)", queue_info.GetId(), queue_info.ToString().CStr());
			return false;
		}

		std::unique_lock<std::shared_mutex> lock(_queue_map_guard);
		_queues[queue_info.GetId()] = queue_metrics;

		return true;
	}

	bool ServerMetrics::OnQueueDeleted(const info::ManagedQueue &queue_info)
	{
		std::unique_lock<std::shared_mutex> lock(_queue_map_guard);

		auto it = _queues.find(queue_info.GetId());
		if (it == _queues.end())
		{
			logtw("Cannot find QueueMetrics(%u/%s) for deleting", queue_info.GetId(), queue_info.ToString().CStr());
			return false;
		}

		_queues.erase(it);

		return true;
	}

	bool ServerMetrics::OnQueueUpdated(const info::ManagedQueue &queue_info, bool with_metadata)
	{
		auto queue = GetQueueMetrics(queue_info);
		if (queue == nullptr)
		{
			logtw("Cannot find QueueMetrics(%u/%s) for updating", queue_info.GetId(), queue_info.ToString().CStr());
			return false;
		}

		if (with_metadata == true)
		{
			queue->UpdateMetadata(queue_info);
		}
		queue->UpdateMetrics(queue_info);

		/**
			[Experimental] Delete lazy stream

			If the size of the queue lasts beyond the limit for N seconds, it is determined to be an invalid stream. 
			For system recovery, the problematic stream is forcibly deleted.

			server.xml:
				<Modules>
					<Recovery>
						<!--  
						If the packet/frame queue is exceed for a certain period of time(millisecond, ms), it will be automatically deleted. 
						If this value is set to zero, the stream will not be deleted. 
						
						-->
						<DeleteLazyStreamTimeout>10000</DeleteLazyStreamTimeout>
					</Recovery>
				</Modules>
		*/
		auto delete_lazy_stream_timeout_conf = _server_config->GetModules().GetRecovery().GetDeleteLazyStreamTimeout();
		if (delete_lazy_stream_timeout_conf > 0)
		{
			if ((queue_info.GetThresholdExceededTimeInUs() > delete_lazy_stream_timeout_conf) && (!queue_info.GetUrn()->GetStreamName().IsEmpty()))
			{
				auto vhost_app	 = queue_info.GetUrn()->GetVHostAppName();
				auto stream_name = queue_info.GetUrn()->GetStreamName();

				logtc("The %s queue has been exceeded for %" PRId64 " ms. stream will be forcibly deleted. VhostApp(%s), Stream(%s)",
					  queue_info.GetUrn()->ToString().CStr(), queue_info.GetThresholdExceededTimeInUs(), vhost_app.CStr(), stream_name.CStr());

				if (vhost_app.IsValid())
				{
					ocst::Orchestrator::GetInstance()->TerminateStream(vhost_app, stream_name);
				}

				// Clear memory fragmentation
				malloc_trim(0);
			}
		}

		return true;
	}

	std::map<uint32_t, std::shared_ptr<QueueMetrics>> ServerMetrics::GetQueueMetricsList()
	{
		std::shared_lock<std::shared_mutex> lock(_queue_map_guard);

		return _queues;
	}

	std::shared_ptr<QueueMetrics> ServerMetrics::GetQueueMetrics(const info::ManagedQueue &queue_info)
	{
		std::shared_lock<std::shared_mutex> lock(_queue_map_guard);

		if (_queues.find(queue_info.GetId()) == _queues.end())
		{
			return nullptr;
		}

		return _queues[queue_info.GetId()];
	}
}  // namespace mon