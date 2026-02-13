//
// Created by getroot on 20. 1. 16.
//

#include "monitoring.h"

#include "monitoring_private.h"

namespace mon
{
	void Monitoring::Release()
	{
		OV_SAFE_RESET(_server_metric, nullptr, _server_metric->Release(), _server_metric);
		_forwarder.Stop();

		if (_alert != nullptr)
		{
			_alert->Stop();
		}
	}

	std::shared_ptr<ServerMetrics> Monitoring::GetServerMetrics()
	{
		return _server_metric;
	}

	std::map<uint32_t, std::shared_ptr<HostMetrics>> Monitoring::GetHostMetricsList()
	{
		return _server_metric->GetHostMetricsList();
	}

	std::shared_ptr<HostMetrics> Monitoring::GetHostMetrics(const info::Host &host_info)
	{
		return _server_metric->GetHostMetrics(host_info);
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

		if (stream.IsInputStream())
		{
			auto stream_metric = app_metric->GetStreamMetrics(stream);
			return stream_metric;
		}
		else
		{
			auto stream_metric = app_metric->GetStreamMetrics(stream);
			if (stream_metric == nullptr)
			{
				return nullptr;
			}

			auto output_stream_metric = app_metric->GetStreamMetrics(stream);
			return output_stream_metric;
		}
	}

	void Monitoring::SetLogPath(const ov::String &log_path)
	{
		_logger.SetLogPath(log_path);
		_forwarder.SetLogPath(log_path);
	}

	void Monitoring::OnServerStarted(const std::shared_ptr<const cfg::Server> &server_config)
	{
		_server_metric	 = std::make_shared<ServerMetrics>(server_config);
		_is_analytics_on = _server_metric->GetConfig()->GetAnalytics().IsParsed();

		_alert			 = std::make_shared<alrt::Alert>();
		_alert->Start(server_config);

		logti("%s(%s) ServerMetric has been started for monitoring - %s",
			  server_config->GetName().CStr(), server_config->GetID().CStr(),
			  ov::Converter::ToISO8601String(_server_metric->GetServerStartedTime()).CStr());

		if (IsAnalyticsOn())
		{
			auto event = Event(EventType::ServerStarted, _server_metric);
			_logger.Write(event);

			_timer.Push(
				[this](void *parameter) -> ov::DelayQueueAction {
					auto event = Event(EventType::ServerStat, _server_metric);
					_logger.Write(event);
					return ov::DelayQueueAction::Repeat;
				},
				5000);

			_timer.Start();

			_forwarder.Start(server_config);
		}
	}

	bool Monitoring::OnHostCreated(const info::Host &host_info)
	{
		if (_server_metric->OnHostCreated(host_info) == false)
		{
			return false;
		}

		if (IsAnalyticsOn())
		{
			auto host_metrics = _server_metric->GetHostMetrics(host_info);
			auto event		  = Event(EventType::HostCreated, _server_metric);
			event.SetExtraMetric(host_metrics);
			_logger.Write(event);
		}

		return true;
	}

	bool Monitoring::OnHostDeleted(const info::Host &host_info)
	{
		if (_server_metric->OnHostDeleted(host_info) == false)
		{
			return false;
		}

		if (IsAnalyticsOn())
		{
			auto host_metrics = _server_metric->GetHostMetrics(host_info);
			auto event		  = Event(EventType::HostDeleted, _server_metric);
			event.SetExtraMetric(host_metrics);
			_logger.Write(event);
		}

		return true;
	}

	bool Monitoring::OnApplicationCreated(const info::Application &app_info)
	{
		auto host_metrics = _server_metric->GetHostMetrics(app_info.GetHostInfo());
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

		if (IsAnalyticsOn())
		{
			auto event = Event(EventType::AppCreated, _server_metric);
			event.SetExtraMetric(app_metrics);
			_logger.Write(event);
		}

		return true;
	}
	bool Monitoring::OnApplicationDeleted(const info::Application &app_info)
	{
		auto host_metrics = _server_metric->GetHostMetrics(app_info.GetHostInfo());
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

		if (IsAnalyticsOn())
		{
			auto event = Event(EventType::AppDeleted, _server_metric);
			event.SetExtraMetric(app_metrics);
			_logger.Write(event);
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
		EventType event_type						  = EventType::StreamCreated;
		if (stream.IsInputStream())
		{
			event_type	   = EventType::StreamCreated;
			stream_metrics = app_metrics->GetStreamMetrics(stream);
			if (stream_metrics == nullptr)
			{
				return false;
			}

			_alert->SendStreamMessage(alrt::Message::Code::INGRESS_STREAM_CREATED, stream_metrics);
		}
		// Output stream created
		else
		{
			event_type	   = EventType::StreamOutputsUpdated;

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

			_alert->SendStreamMessage(alrt::Message::Code::EGRESS_STREAM_CREATED, output_stream_metric);
		}

		if (IsAnalyticsOn())
		{
			auto event = Event(event_type, _server_metric);
			event.SetExtraMetric(stream_metrics);
			_logger.Write(event);
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

			_alert->SendStreamMessage(alrt::Message::Code::INGRESS_STREAM_CREATION_FAILED_DUPLICATE_NAME, stream_metrics);
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

			_alert->SendStreamMessage(alrt::Message::Code::INGRESS_STREAM_PREPARED, stream_metrics);
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

			_alert->SendStreamMessage(alrt::Message::Code::EGRESS_STREAM_PREPARED, output_stream_metric);
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

				_alert->SendStreamMessage(alrt::Message::Code::INGRESS_STREAM_DELETED, stream_metrics);
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

				_alert->SendStreamMessage(alrt::Message::Code::EGRESS_STREAM_DELETED, output_stream_metric);
			}

			if (app_metrics->OnStreamDeleted(stream) == false)
			{
				return false;
			}
		}

		if (IsAnalyticsOn())
		{
			if (stream_metrics->IsInputStream())
			{
				auto event = Event(EventType::StreamDeleted, _server_metric);
				event.SetExtraMetric(stream_metrics);
				_logger.Write(event);
			}
		}

		return true;
	}

	bool Monitoring::OnStreamUpdated(const info::Stream &stream_info)
	{
		return true;
	}

	void Monitoring::IncreaseBytesIn(const info::Stream &stream_info, uint64_t value)
	{
		auto host_metric = _server_metric->GetHostMetrics(stream_info.GetApplicationInfo().GetHostInfo());
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

		_server_metric->IncreaseBytesIn(value);
		host_metric->IncreaseBytesIn(value);
		app_metric->IncreaseBytesIn(value);
		stream_metric->IncreaseBytesIn(value);
	}

	void Monitoring::IncreaseBytesOut(const info::Stream &stream_info, PublisherType type, uint64_t value)
	{
		auto host_metric = _server_metric->GetHostMetrics(stream_info.GetApplicationInfo().GetHostInfo());
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

		_server_metric->IncreaseBytesOut(type, value);
		host_metric->IncreaseBytesOut(type, value);
		app_metric->IncreaseBytesOut(type, value);
		stream_metric->IncreaseBytesOut(type, value);
	}

	void Monitoring::OnSessionConnected(const info::Stream &stream_info, PublisherType type)
	{
		auto host_metric = _server_metric->GetHostMetrics(stream_info.GetApplicationInfo().GetHostInfo());
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

		_server_metric->OnSessionConnected(type);
		host_metric->OnSessionConnected(type);
		app_metric->OnSessionConnected(type);
		stream_metric->OnSessionConnected(type);
	}

	void Monitoring::OnSessionDisconnected(const info::Stream &stream_info, PublisherType type)
	{
		auto server_metric = _server_metric;
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
		auto server_metric = _server_metric;
		if (server_metric == nullptr)
		{
			return;
		}

		auto host_metric = _server_metric->GetHostMetrics(stream_info.GetApplicationInfo().GetHostInfo());
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

		_server_metric->OnSessionsDisconnected(type, number_of_sessions);
		host_metric->OnSessionsDisconnected(type, number_of_sessions);
		app_metric->OnSessionsDisconnected(type, number_of_sessions);
		stream_metric->OnSessionsDisconnected(type, number_of_sessions);
	}

	std::shared_ptr<alrt::Alert> Monitoring::GetAlert()
	{
		return _alert;
	}

}  // namespace mon