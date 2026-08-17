#pragma once

#include <variant>

#include "base/ovlibrary/ovlibrary.h"
#include "server_metrics.h"

#define EVENT_VERSION 1
namespace mon
{
	enum class EventType
	{
		// StreamEventType
		ServerStarted,
		HostCreated,
		HostDeleted,
		AppCreated,
		AppDeleted,
		StreamCreated,
		StreamDeleted,
		StreamOriginLinkUpdated,
		StreamOutputsUpdated,
		// SessionEventType
		SessionConnected,
		SessionDisconnected,
		// ActionEventType
		ApiCalled,
		RecordingStarted,
		RecordingStopped,
		PushStarted,
		PushStopped,
		// NotificationEventType
		Info,
		Error,
		// StatisticsEventType
		ServerStat
	};

	using ExtraValueType = std::variant<int, float, bool, ov::String>;
	class Event
	{
	public:
		Event(EventType type, const std::shared_ptr<ServerMetrics> &server_metric);

		uint64_t GetCreationTimeMSec() const;
		EventType GetType() const;
		ov::String GetTypeString() const;

		void SetExtraMetric(const std::shared_ptr<HostMetrics> &host_metric);
		void SetExtraMetric(const std::shared_ptr<ApplicationMetrics> &app_metric);
		void SetExtraMetric(const std::shared_ptr<StreamMetrics> &stream_metric);

		void SetMessage(const ov::String &message);

		void AddExtraData(const ov::String key, const ExtraValueType value);

		ov::String SerializeToJson() const;

	private:
		// Use _extra_metric_type
		bool FillProducer(Json::Value &json_producer) const;
		bool FillProducer(Json::Value &json_producer, const std::shared_ptr<HostMetrics> &host_metric) const;
		bool FillProducer(Json::Value &json_producer, const std::shared_ptr<ApplicationMetrics> &app_metric) const;
		bool FillProducer(Json::Value &json_producer, const std::shared_ptr<StreamMetrics> &stream_metric) const;

		// Use _extra_metric_type
		bool FillServerInfo(Json::Value &json_server) const;
		bool FillHostInfo(Json::Value &json_host, const std::shared_ptr<HostMetrics> &host_metric) const;
		bool FillHostAppInfo(Json::Value &json_host, const std::shared_ptr<ApplicationMetrics> &app_metric) const;
		bool FillHostAppStreamInfo(Json::Value &json_host, const std::shared_ptr<StreamMetrics> &stream_metric) const;

		bool FillServerStatistics(Json::Value &json_stat) const;

		enum class EventCategory
		{
			StreamEventType,
			SessionEventType,
			ActionEventType,
			NotificationEventType,
			StatisticsEventType
		};

		EventType _type;
		EventCategory _category;
		ov::String _message;

		enum class ExtraMetricType
		{
			None,
			HostMetric,
			AppMetric,
			StreamMetric
		};

		uint64_t _creation_time_msec = 0;

		// Essential metic
		std::shared_ptr<ServerMetrics> _server_metric;

		// Optional metric
		ExtraMetricType _extra_metric_type				= ExtraMetricType::None;
		std::shared_ptr<HostMetrics> _host_metric		= nullptr;
		std::shared_ptr<ApplicationMetrics> _app_metric = nullptr;
		std::shared_ptr<StreamMetrics> _stream_metric	= nullptr;

		std::map<ov::String, ExtraValueType> _extra_data;
	};
}  // namespace mon