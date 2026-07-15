//
// Created by getroot on 20. 1. 16.
//

#include "monitoring.h"

#include "monitoring_private.h"

namespace mon
{
	void Monitoring::Release()
	{
		std::shared_ptr<ServerMetrics> to_release;
		{
			std::unique_lock<std::shared_mutex> lock(_server_metric_guard);
			to_release = std::move(_server_metric);
			_server_metric = nullptr;
		}
		if (to_release != nullptr)
		{
			to_release->Release();
		}

		std::shared_ptr<alrt::Alert> alert_to_stop;
		{
			std::unique_lock<std::shared_mutex> lock(_alert_guard);
			alert_to_stop = std::move(_alert);
			_alert = nullptr;
		}
		if (alert_to_stop != nullptr)
		{
			alert_to_stop->Stop();
		}
	}

	std::shared_ptr<ServerMetrics> Monitoring::GetServerMetrics()
	{
		std::shared_lock<std::shared_mutex> lock(_server_metric_guard);
		return _server_metric;
	}

	std::map<uint32_t, std::shared_ptr<HostMetrics>> Monitoring::GetHostMetricsList()
	{
		auto server_metric = GetServerMetrics();
		if (server_metric == nullptr)
		{
			return {};
		}
		return server_metric->GetHostMetricsList();
	}

	std::map<uint32_t, std::shared_ptr<QueueMetrics>> Monitoring::GetQueueMetricsList()
	{
		auto server_metric = GetServerMetrics();
		if (server_metric == nullptr)
		{
			return {};
		}
		return server_metric->GetQueueMetricsList();
	}

	std::shared_ptr<HostMetrics> Monitoring::GetHostMetrics(const info::Host &host_info)
	{
		auto server_metric = GetServerMetrics();
		if (server_metric == nullptr)
		{
			return nullptr;
		}
		return server_metric->GetHostMetrics(host_info);
	}

	std::shared_ptr<ApplicationMetrics> Monitoring::GetApplicationMetrics(const info::Application &app_info)
	{
		auto host_metric = GetHostMetrics(app_info.GetHostInfo());
		if (host_metric == nullptr)
		{
			return nullptr;
		}

		auto app_metric = host_metric->GetApplicationMetrics(app_info);
		if (app_metric == nullptr)
		{
			return nullptr;
		}

		return app_metric;
	}

	std::shared_ptr<StreamMetrics> Monitoring::GetStreamMetrics(const info::Stream &stream)
	{
		auto app_metric = GetApplicationMetrics(stream.GetApplicationInfo());
		if (app_metric == nullptr)
		{
			return nullptr;
		}

		return app_metric->GetStreamMetrics(stream);
	}

	void Monitoring::OnServerStarted(const std::shared_ptr<const cfg::Server> &server_config)
	{
		{
			std::unique_lock<std::shared_mutex> lock(_server_metric_guard);
			_server_metric = std::make_shared<ServerMetrics>(server_config);
		}

		auto server_metric = GetServerMetrics();
		if (server_metric == nullptr)
		{
			return;
		}

		{
			std::unique_lock<std::shared_mutex> lock(_alert_guard);
			_alert = std::make_shared<alrt::Alert>();
			_alert->Start(server_config);
		}

		logti("%s(%s) ServerMetric has been started for monitoring - %s",
			  server_config->GetName().CStr(), server_config->GetID().CStr(),
			  ov::Converter::ToISO8601String(server_metric->GetServerStartedTime()).CStr());
	}

	bool Monitoring::OnHostCreated(const info::Host &host_info)
	{
		auto server_metric = GetServerMetrics();
		if (server_metric == nullptr)
		{
			return false;
		}

		if (server_metric->OnHostCreated(host_info) == false)
		{
			return false;
		}

		return true;
	}

	bool Monitoring::OnHostDeleted(const info::Host &host_info)
	{
		auto server_metric = GetServerMetrics();
		if (server_metric == nullptr)
		{
			return false;
		}

		if (server_metric->OnHostDeleted(host_info) == false)
		{
			return false;
		}

		return true;
	}

	bool Monitoring::OnApplicationCreated(const info::Application &app_info)
	{
		auto server_metric = GetServerMetrics();
		if (server_metric == nullptr)
		{
			return false;
		}

		auto host_metrics = server_metric->GetHostMetrics(app_info.GetHostInfo());
		if (host_metrics == nullptr)
		{
			return false;
		}

		if (host_metrics->OnApplicationCreated(app_info) == false)
		{
			return false;
		}

		auto app_metrics = host_metrics->GetApplicationMetrics(app_info);
		if (app_metrics == nullptr)
		{
			return false;
		}

		return true;
	}
	bool Monitoring::OnApplicationDeleted(const info::Application &app_info)
	{
		auto server_metric = GetServerMetrics();
		if (server_metric == nullptr)
		{
			return false;
		}

		auto host_metrics = server_metric->GetHostMetrics(app_info.GetHostInfo());
		if (host_metrics == nullptr)
		{
			return false;
		}
		auto app_metrics = host_metrics->GetApplicationMetrics(app_info);
		if (app_metrics == nullptr)
		{
			return false;
		}

		if (host_metrics->OnApplicationDeleted(app_info) == false)
		{
			return false;
		}

		return true;
	}
	bool Monitoring::OnStreamCreated(const info::Stream &stream)
	{
		auto app_metrics = GetApplicationMetrics(stream.GetApplicationInfo());
		if (app_metrics == nullptr)
		{
			return false;
		}

		if (app_metrics->OnStreamCreated(stream) == false)
		{
			return false;
		}

		// Writes events only based on the input stream.
		std::shared_ptr<StreamMetrics> stream_metrics = nullptr;
		if (stream.IsInputStream())
		{
			stream_metrics = app_metrics->GetStreamMetrics(stream);
			if (stream_metrics == nullptr)
			{
				return false;
			}

			SendStreamAlertMessage(alrt::Message::Code::INGRESS_STREAM_CREATED, stream_metrics);
		}
		// Output stream created
		else
		{
			// Get Input Stream
			stream_metrics = app_metrics->GetStreamMetrics(*stream.GetLinkedInputStream());
			if (stream_metrics == nullptr)
			{
				return false;
			}

			// Link output stream to input stream
			auto output_stream_metric = app_metrics->GetStreamMetrics(stream);
			if (output_stream_metric == nullptr)
			{
				return false;
			}
			stream_metrics->LinkOutputStreamMetrics(output_stream_metric);

			SendStreamAlertMessage(alrt::Message::Code::EGRESS_STREAM_CREATED, output_stream_metric);
		}

		return true;
	}

	bool Monitoring::OnStreamCreationFailed(const info::Stream &stream)
	{
		if (stream.IsInputStream())
		{
			auto app_metrics = GetApplicationMetrics(stream.GetApplicationInfo());
			if (app_metrics == nullptr)
			{
				return false;
			}

			auto stream_metrics = std::make_shared<StreamMetrics>(app_metrics, stream);
			if (stream_metrics == nullptr)
			{
				logte("Cannot create StreamMetrics (%s - %s)", stream.GetUri().CStr(), stream.GetUUID().CStr());
				return false;
			}

			SendStreamAlertMessage(alrt::Message::Code::INGRESS_STREAM_CREATION_FAILED_DUPLICATE_NAME, stream_metrics);
		}

		return true;
	}

	bool Monitoring::OnStreamPrepared(const info::Stream &stream)
	{
		auto app_metrics = GetApplicationMetrics(stream.GetApplicationInfo());
		if (app_metrics == nullptr)
		{
			return false;
		}

		if (stream.IsInputStream())
		{
			auto stream_metrics = app_metrics->GetStreamMetrics(stream);
			if (stream_metrics == nullptr)
			{
				return false;
			}

			SendStreamAlertMessage(alrt::Message::Code::INGRESS_STREAM_PREPARED, stream_metrics);
		}
		else
		{
			auto output_stream_metric = app_metrics->GetStreamMetrics(stream);
			if (output_stream_metric == nullptr)
			{
				return false;
			}

			// Update module usage count for the tracks in the stream if the stream is an output stream
			for (const auto &[id, track] : stream.GetTracks())
			{
				output_stream_metric->IncreaseModuleUsageCount(track);
			}

			SendStreamAlertMessage(alrt::Message::Code::EGRESS_STREAM_PREPARED, output_stream_metric);
		}

		return true;
	}

	bool Monitoring::OnStreamDeleted(const info::Stream &stream)
	{
		auto app_metrics = GetApplicationMetrics(stream.GetApplicationInfo());
		if (app_metrics == nullptr)
		{
			return false;
		}

		std::shared_ptr<StreamMetrics> stream_metrics = nullptr;
		//TODO(Getroot): If a session connects or disconnects at the moment the block below is executed, a race condition may occur, so it must be protected with a mutex.
		{
			// If there are sessions in the stream, the number of visitors to the app is recalculated.
			// Calculate connections to application only if it hasn't origin stream to prevent double subtract.
			if (stream.IsInputStream())
			{
				stream_metrics = app_metrics->GetStreamMetrics(stream);
				if (stream_metrics == nullptr)
				{
					return false;
				}

				for (uint8_t type = static_cast<uint8_t>(PublisherType::Unknown); type < static_cast<uint8_t>(PublisherType::NumberOfPublishers); type++)
				{
					OnSessionsDisconnected(*stream_metrics, static_cast<PublisherType>(type), stream_metrics->GetConnections(static_cast<PublisherType>(type)));
				}

				SendStreamAlertMessage(alrt::Message::Code::INGRESS_STREAM_DELETED, stream_metrics);
			}
			else
			{
				auto output_stream_metric = app_metrics->GetStreamMetrics(stream);
				if (output_stream_metric == nullptr)
				{
					return false;
				}

				// Update module usage count for the tracks in the stream if the stream is an output stream
				for (const auto &[id, track] : stream.GetTracks())
				{
					output_stream_metric->DecreaseModuleUsageCount(track);
				}

				SendStreamAlertMessage(alrt::Message::Code::EGRESS_STREAM_DELETED, output_stream_metric);
			}

			if (app_metrics->OnStreamDeleted(stream) == false)
			{
				return false;
			}
		}

		return true;
	}

	bool Monitoring::OnStreamUpdated(const info::Stream &stream_info)
	{
		return true;
	}

	bool Monitoring::OnQueueCreated(const info::ManagedQueue &queue_info)
	{
		auto server_metric = GetServerMetrics();
		if (server_metric == nullptr)
		{
			// Server is shutting down, treat as success to stop retry loop
			return true;
		}
		return server_metric->OnQueueCreated(queue_info);
	}

	void Monitoring::OnQueueDeleted(const info::ManagedQueue &queue_info)
	{
		auto server_metric = GetServerMetrics();
		if (server_metric == nullptr)
		{
			return;
		}
		server_metric->OnQueueDeleted(queue_info);
	}

	void Monitoring::OnQueueUpdated(const info::ManagedQueue &queue_info, bool with_metadata)
	{
		auto server_metric = GetServerMetrics();
		if (server_metric == nullptr)
		{
			return;
		}
		server_metric->OnQueueUpdated(queue_info, with_metadata);
	}

	void Monitoring::IncreaseBytesIn(const info::Stream &stream_info, uint64_t value)
	{
		auto server_metric = GetServerMetrics();
		if (server_metric == nullptr)
		{
			return;
		}
		auto host_metric = server_metric->GetHostMetrics(stream_info.GetApplicationInfo().GetHostInfo());
		if (host_metric == nullptr)
		{
			return;
		}
		auto app_metric = host_metric->GetApplicationMetrics(stream_info.GetApplicationInfo());
		if (app_metric == nullptr)
		{
			return;
		}
		auto stream_metric = app_metric->GetStreamMetrics(stream_info);
		if (stream_metric == nullptr)
		{
			return;
		}

		server_metric->IncreaseBytesIn(value);
		host_metric->IncreaseBytesIn(value);
		app_metric->IncreaseBytesIn(value);
		stream_metric->IncreaseBytesIn(value);
	}

	void Monitoring::IncreaseBytesOut(const info::Stream &stream_info, PublisherType type, uint64_t value)
	{
		auto server_metric = GetServerMetrics();
		if (server_metric == nullptr)
		{
			return;
		}
		auto host_metric = server_metric->GetHostMetrics(stream_info.GetApplicationInfo().GetHostInfo());
		if (host_metric == nullptr)
		{
			return;
		}
		auto app_metric = host_metric->GetApplicationMetrics(stream_info.GetApplicationInfo());
		if (app_metric == nullptr)
		{
			return;
		}
		auto stream_metric = app_metric->GetStreamMetrics(stream_info);
		if (stream_metric == nullptr)
		{
			return;
		}

		server_metric->IncreaseBytesOut(type, value);
		host_metric->IncreaseBytesOut(type, value);
		app_metric->IncreaseBytesOut(type, value);
		stream_metric->IncreaseBytesOut(type, value);
	}

	void Monitoring::OnSessionConnected(const info::Stream &stream_info, PublisherType type)
	{
		auto server_metric = GetServerMetrics();
		if (server_metric == nullptr)
		{
			return;
		}
		auto host_metric = server_metric->GetHostMetrics(stream_info.GetApplicationInfo().GetHostInfo());
		if (host_metric == nullptr)
		{
			return;
		}
		auto app_metric = host_metric->GetApplicationMetrics(stream_info.GetApplicationInfo());
		if (app_metric == nullptr)
		{
			return;
		}
		auto stream_metric = app_metric->GetStreamMetrics(stream_info);
		if (stream_metric == nullptr)
		{
			return;
		}

		server_metric->OnSessionConnected(type);
		host_metric->OnSessionConnected(type);
		app_metric->OnSessionConnected(type);
		stream_metric->OnSessionConnected(type);
	}

	void Monitoring::OnSessionDisconnected(const info::Stream &stream_info, PublisherType type)
	{
		auto server_metric = GetServerMetrics();
		if (server_metric == nullptr)
		{
			return;
		}

		auto host_metric = server_metric->GetHostMetrics(stream_info.GetApplicationInfo().GetHostInfo());
		if (host_metric == nullptr)
		{
			return;
		}
		auto app_metric = host_metric->GetApplicationMetrics(stream_info.GetApplicationInfo());
		if (app_metric == nullptr)
		{
			return;
		}
		auto stream_metric = app_metric->GetStreamMetrics(stream_info);
		if (stream_metric == nullptr)
		{
			return;
		}

		server_metric->OnSessionDisconnected(type);
		host_metric->OnSessionDisconnected(type);
		app_metric->OnSessionDisconnected(type);
		stream_metric->OnSessionDisconnected(type);
	}

	void Monitoring::OnSessionsDisconnected(const info::Stream &stream_info, PublisherType type, uint64_t number_of_sessions)
	{
		auto server_metric = GetServerMetrics();
		if (server_metric == nullptr)
		{
			return;
		}

		auto host_metric = server_metric->GetHostMetrics(stream_info.GetApplicationInfo().GetHostInfo());
		if (host_metric == nullptr)
		{
			return;
		}
		auto app_metric = host_metric->GetApplicationMetrics(stream_info.GetApplicationInfo());
		if (app_metric == nullptr)
		{
			return;
		}
		auto stream_metric = app_metric->GetStreamMetrics(stream_info);
		if (stream_metric == nullptr)
		{
			return;
		}

		server_metric->OnSessionsDisconnected(type, number_of_sessions);
		host_metric->OnSessionsDisconnected(type, number_of_sessions);
		app_metric->OnSessionsDisconnected(type, number_of_sessions);
		stream_metric->OnSessionsDisconnected(type, number_of_sessions);
	}

	std::shared_ptr<alrt::Alert> Monitoring::GetAlert()
	{
		std::shared_lock<std::shared_mutex> lock(_alert_guard);
		return _alert;
	}

	void Monitoring::SendStreamAlertMessage(alrt::Message::Code code, const std::shared_ptr<StreamMetrics> &stream_metric, const std::shared_ptr<StreamMetrics> &parent_stream_metric, const std::shared_ptr<alrt::ExtraData> &extra)
	{
		std::shared_lock<std::shared_mutex> lock(_alert_guard);
		if (_alert != nullptr)
		{
			_alert->SendStreamMessage(code, stream_metric, parent_stream_metric, extra);
		}
	}

	void Monitoring::SendStreamAlertMessage(alrt::Message::Code code, const std::shared_ptr<StreamMetrics> &stream_metric)
	{
		std::shared_lock<std::shared_mutex> lock(_alert_guard);
		if (_alert != nullptr)
		{
			_alert->SendStreamMessage(code, stream_metric);
		}
	}

}  // namespace mon