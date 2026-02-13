#include "event.h"

#include <base/info/ome_version.h>
#include <main/git_info.h>
#include <main/main.h>
#include <modules/json_serdes/converters.h>

#include "monitoring_private.h"

namespace mon
{
	Event::Event(EventType type, const std::shared_ptr<ServerMetrics> &server_metric)
		: _type(type), _server_metric(server_metric)
	{
		_creation_time_msec = ov::Clock::NowMSec();

		switch (_type)
		{
			// StreamEventType
			case EventType::ServerStarted:
			case EventType::HostCreated:
			case EventType::HostDeleted:
			case EventType::AppCreated:
			case EventType::AppDeleted:
			case EventType::StreamCreated:
			case EventType::StreamDeleted:
			case EventType::StreamOriginLinkUpdated:
			case EventType::StreamOutputsUpdated:
				_category = EventCategory::StreamEventType;
				break;
			// SessionEventType
			case EventType::SessionConnected:
			case EventType::SessionDisconnected:
				_category = EventCategory::SessionEventType;
				break;
			// ActionEventType
			case EventType::ApiCalled:
			case EventType::RecordingStarted:
			case EventType::RecordingStopped:
			case EventType::PushStarted:
			case EventType::PushStopped:
				_category = EventCategory::ActionEventType;
				break;
			// NotificationEventType
			case EventType::Info:
			case EventType::Error:
				_category = EventCategory::NotificationEventType;
				break;
			// StatisticsEventType
			case EventType::ServerStat:
				_category = EventCategory::StatisticsEventType;
				break;
		}
	}

	uint64_t Event::GetCreationTimeMSec() const
	{
		return _creation_time_msec;
	}

	EventType Event::GetType() const
	{
		return _type;
	}

	ov::String Event::GetTypeString() const
	{
		switch (_type)
		{
			// StreamEventType
			case EventType::ServerStarted:
				return "ServerStarted";
			case EventType::HostCreated:
				return "HostCreated";
			case EventType::HostDeleted:
				return "HostDeleted";
			case EventType::AppCreated:
				return "AppCreated";
			case EventType::AppDeleted:
				return "AppDeleted";
			case EventType::StreamCreated:
				return "StreamCreated";
			case EventType::StreamDeleted:
				return "StreamDeleted";
			case EventType::StreamOriginLinkUpdated:
				return "StreamOriginLinkUpdated";
			case EventType::StreamOutputsUpdated:
				return "StreamOutputsUpdated";
			// SessionEventType
			case EventType::SessionConnected:
				return "SessionConnected";
			case EventType::SessionDisconnected:
				return "SessionDisconnected";
			// ActionEventType
			case EventType::ApiCalled:
				return "ApiCalled";
			case EventType::RecordingStarted:
				return "RecordingStarted";
			case EventType::RecordingStopped:
				return "RecordingStopped";
			case EventType::PushStarted:
				return "PushStarted";
			case EventType::PushStopped:
				return "PushStopped";
			// NotificationEventType
			case EventType::Info:
				return "Info";
			case EventType::Error:
				return "Error";
			// StatisticsEventType
			case EventType::ServerStat:
				return "ServerStat";
		}

		return "";
	}

	void Event::SetExtraMetric(const std::shared_ptr<HostMetrics> &host_metric)
	{
		_extra_metric_type = ExtraMetricType::HostMetric;
		_host_metric	   = host_metric;
	}

	void Event::SetExtraMetric(const std::shared_ptr<ApplicationMetrics> &app_metric)
	{
		_extra_metric_type = ExtraMetricType::AppMetric;
		_app_metric		   = app_metric;
	}

	void Event::SetExtraMetric(const std::shared_ptr<StreamMetrics> &stream_metric)
	{
		_extra_metric_type = ExtraMetricType::StreamMetric;
		_stream_metric	   = stream_metric;
	}

	void Event::SetMessage(const ov::String &message)
	{
		_message = message;
	}

	void Event::AddExtraData(const ov::String key, const ExtraValueType value)
	{
		_extra_data.emplace(key, value);
	}

	ov::String Event::SerializeToJson() const
	{
		Json::Value json_root;

		// Fill common values
		json_root["eventVersion"]	 = EVENT_VERSION;
		json_root["timestampMillis"] = _creation_time_msec;
		json_root["userKey"]		 = _server_metric->GetConfig()->GetAnalytics().GetUserKey().CStr();
		json_root["serverID"]		 = _server_metric->GetConfig()->GetID().CStr();

		// Fill root["event"]
		Json::Value &json_event		 = json_root["event"];
		json_event["type"]			 = GetTypeString().CStr();
		if (_message.IsEmpty() == false)
		{
			json_event["message"] = _message.CStr();
		}

		Json::Value &json_producer = json_event["producer"];
		FillProducer(json_producer);

		// Fill root["data"]
		Json::Value &json_data = json_root["data"];

		if (_category == EventCategory::StreamEventType)
		{
			//TODO(Getroot): Implement this
			json_data["dataType"]		  = "Not Implemented";

			Json::Value &json_server_info = json_data["serverInfo"];
			FillServerInfo(json_server_info);
		}
		else if (_category == EventCategory::StatisticsEventType)
		{
			Json::Value &json_server_stat = json_data["serverStat"];
			FillServerStatistics(json_server_stat);
		}

		Json::StreamWriterBuilder builder;
		/* 
			Default StreamWriterBuilder settings

		  	(*settings)["commentStyle"] = "All";
			(*settings)["indentation"] = "\t";
			(*settings)["enableYAMLCompatibility"] = false;
			(*settings)["dropNullPlaceholders"] = false;
			(*settings)["useSpecialFloats"] = false;
			(*settings)["emitUTF8"] = false;
			(*settings)["precision"] = 17;
			(*settings)["precisionType"] = "significant";
		*/

		logtt("%s", json_root.toStyledString().c_str());

		builder["indentation"] = "";
		return Json::writeString(builder, json_root).c_str();
	}

	bool Event::FillProducer(Json::Value &json_producer) const
	{
		json_producer["serverID"] = _server_metric->GetConfig()->GetID().CStr();

		switch (_extra_metric_type)
		{
			case ExtraMetricType::HostMetric:
				FillProducer(json_producer, _host_metric);
				break;
			case ExtraMetricType::AppMetric:
				FillProducer(json_producer, _app_metric);
				break;
			case ExtraMetricType::StreamMetric:
				FillProducer(json_producer, _stream_metric);
				break;
			case ExtraMetricType::None:
			default:
				break;
		}

		return true;
	}

	bool Event::FillProducer(Json::Value &jsonProducer, const std::shared_ptr<HostMetrics> &host_metric) const
	{
		jsonProducer["serverID"] = _server_metric->GetConfig()->GetID().CStr();
		jsonProducer["hostID"]	 = host_metric->GetUUID().CStr();
		return true;
	}

	bool Event::FillProducer(Json::Value &jsonProducer, const std::shared_ptr<ApplicationMetrics> &app_metric) const
	{
		FillProducer(jsonProducer, app_metric->GetHostMetrics());
		jsonProducer["appID"] = app_metric->GetUUID().CStr();
		return true;
	}

	bool Event::FillProducer(Json::Value &jsonProducer, const std::shared_ptr<StreamMetrics> &stream_metric) const
	{
		FillProducer(jsonProducer, stream_metric->GetApplicationMetrics());
		jsonProducer["streamID"] = stream_metric->GetUUID().CStr();
		return true;
	}

	bool Event::FillServerInfo(Json::Value &json_server) const
	{
		json_server["serverID"]		 = _server_metric->GetConfig()->GetID().CStr();
		json_server["serverName"]	 = _server_metric->GetConfig()->GetName().CStr();
		json_server["serverVersion"] = info::OmeVersion::GetInstance()->ToString().CStr();
		json_server["startedTime"]	 = ov::Converter::ToISO8601String(std::chrono::system_clock::now()).CStr();
		json_server["bind"]			 = _server_metric->GetConfig()->GetBind().ToJson();

		Json::Value &json_host		 = json_server["host"];
		switch (_extra_metric_type)
		{
			case ExtraMetricType::HostMetric:
				FillHostInfo(json_host, _host_metric);
				break;
			case ExtraMetricType::AppMetric:
				FillHostAppInfo(json_host, _app_metric);
				break;
			case ExtraMetricType::StreamMetric:
				FillHostAppStreamInfo(json_host, _stream_metric);
				break;
			case ExtraMetricType::None:
			default:
				break;
		}

		return true;
	}

	bool Event::FillHostInfo(Json::Value &json_host, const std::shared_ptr<HostMetrics> &host_metric) const
	{
		json_host["hostID"]		  = host_metric->GetUUID().CStr();
		json_host["name"]		  = host_metric->GetName().CStr();
		json_host["createdTime"]  = ov::Converter::ToISO8601String(host_metric->CommonMetrics::GetCreatedTime()).CStr();
		json_host["distribution"] = host_metric->GetDistribution().IsEmpty() ? "ovenmediaengine.com" : host_metric->GetDistribution().CStr();
		json_host["hostNames"]	  = host_metric->GetHost().ToJson();

		return true;
	}

	bool Event::FillHostAppInfo(Json::Value &json_host, const std::shared_ptr<ApplicationMetrics> &app_metric) const
	{
		auto host_metric = app_metric->GetHostMetrics();
		FillHostInfo(json_host, host_metric);

		Json::Value json_app;
		json_app["appID"]		   = app_metric->GetUUID().CStr();
		json_app["name"]		   = app_metric->GetVHostAppName().CStr();
		json_app["createdTime"]	   = ov::Converter::ToISO8601String(app_metric->CommonMetrics::GetCreatedTime()).CStr();
		json_app["outputProfiles"] = app_metric->GetConfig().GetOutputProfiles().ToJson();
		json_app["providers"]	   = app_metric->GetConfig().GetProviders().ToJson();
		json_app["publishers"]	   = app_metric->GetConfig().GetPublishers().ToJson();

		json_host["app"]		   = json_app;

		return true;
	}

	bool Event::FillHostAppStreamInfo(Json::Value &json_host, const std::shared_ptr<StreamMetrics> &stream_metric) const
	{
		auto app_metric = stream_metric->GetApplicationMetrics();
		FillHostAppInfo(json_host, app_metric);

		Json::Value &json_stream   = json_host["app"]["stream"];

		json_stream["streamID"]	   = stream_metric->GetUUID().CStr();
		json_stream["name"]		   = stream_metric->GetName().CStr();
		json_stream["createdTime"] = ov::Converter::ToISO8601String(stream_metric->CommonMetrics::GetCreatedTime()).CStr();

		Json::Value &json_source   = json_stream["source"];

		// OVT, RTSPPull, RTMP, WEBRTC, SRT, MPEG-TS
		json_source["type"]		   = StringFromStreamSourceType(stream_metric->GetSourceType()).CStr();
		json_source["address"]	   = stream_metric->GetMediaSource().CStr();

		// If stream_metric is from OVT provider, originUUID is UUID of origin server's stream
		if (stream_metric->GetOriginStreamUUID().IsEmpty())
		{
			// This stream itself is the origin stream
			json_source["streamID"] = stream_metric->GetUUID().CStr();
		}
		else
		{
			json_source["streamID"] = stream_metric->GetOriginStreamUUID().CStr();
		}

		json_stream["tracks"] = serdes::JsonFromTracks(stream_metric->GetTracks());

		if (stream_metric->GetLinkedOutputStreamMetrics().empty() == false)
		{
			Json::Value &json_outputs = json_stream["outputs"];

			for (const auto &output_stream_metric : stream_metric->GetLinkedOutputStreamMetrics())
			{
				Json::Value json_output_stream;

				json_output_stream["streamID"]	  = output_stream_metric->GetUUID().CStr();
				json_output_stream["name"]		  = output_stream_metric->GetName().CStr();
				json_output_stream["createdTime"] = ov::Converter::ToISO8601String(output_stream_metric->CommonMetrics::GetCreatedTime()).CStr();
				json_output_stream["tracks"]	  = serdes::JsonFromTracks(output_stream_metric->GetTracks());

				json_outputs.append(json_output_stream);
			}
		}

		return true;
	}

	bool Event::FillServerStatistics(Json::Value &json_server_stat) const
	{
		json_server_stat["serverID"]   = _server_metric->GetConfig()->GetID().CStr();
		json_server_stat["serverName"] = _server_metric->GetConfig()->GetName().CStr();
		json_server_stat["stat"]	   = serdes::JsonFromMetrics(_server_metric);
		Json::Value &json_hosts		   = json_server_stat["hosts"];

		for (const auto &[host_key, host_metric] : _server_metric->GetHostMetricsList())
		{
			Json::Value json_host;

			json_host["hostID"]		  = host_metric->GetUUID().CStr();
			json_host["hostName"]	  = host_metric->GetName().CStr();
			json_host["distribution"] = host_metric->GetDistribution().CStr();
			json_host["stat"]		  = serdes::JsonFromMetrics(host_metric);

			Json::Value &json_apps	  = json_host["apps"];

			for (const auto &[app_key, app_metric] : host_metric->GetApplicationMetricsList())
			{
				Json::Value json_app;

				json_app["appID"]		  = app_metric->GetUUID().CStr();
				json_app["appName"]		  = app_metric->GetVHostAppName().CStr();
				json_app["stat"]		  = serdes::JsonFromMetrics(app_metric);

				Json::Value &json_streams = json_app["streams"];
				for (const auto &[stream_key, stream_metric] : app_metric->GetStreamMetricsMap())
				{
					if (stream_metric->IsInputStream())
					{
						Json::Value json_stream;
						json_stream["streamID"]			 = stream_metric->GetUUID().CStr();
						json_stream["streamName"]		 = stream_metric->GetName().CStr();
						json_stream["stat"]				 = serdes::JsonFromMetrics(stream_metric);

						Json::Value &json_output_streams = json_stream["outputs"];
						for (const auto &output_stream_metric : stream_metric->GetLinkedOutputStreamMetrics())
						{
							Json::Value json_output_stream;

							json_output_stream["streamID"]	 = output_stream_metric->GetUUID().CStr();
							json_output_stream["streamName"] = output_stream_metric->GetName().CStr();
							json_output_stream["stat"]		 = serdes::JsonFromMetrics(output_stream_metric);

							json_output_streams.append(json_output_stream);
						}

						json_streams.append(json_stream);
					}
				}

				json_apps.append(json_app);
			}

			json_hosts.append(json_host);
		}

		return true;
	}
}  // namespace mon