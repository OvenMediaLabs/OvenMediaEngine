//
// Created by getroot on 20. 1. 16.
//

#include "application_metrics.h"

#include "host_metrics.h"
#include "monitoring.h"
#include "monitoring_private.h"

namespace mon
{
	ov::String ApplicationMetrics::GetInfoString(bool show_children)
	{
		ov::String out_str = ov::String::FormatString("\n[Application Info]\nid(%u), name(%s)\nCreated Time (%s)\n",
													  GetId(), GetVHostAppName().CStr(),
													  ov::Converter::ToString(_created_time).CStr());

		out_str.Append(CommonMetrics::GetInfoString());

		if (show_children)
		{
			std::shared_lock<std::shared_mutex> lock(_streams_guard);
			for (auto &t : _streams)
			{
				auto stream = t.second;
				out_str.Append(stream->GetInfoString());
			}
		}

		return out_str;
	}

	void ApplicationMetrics::ShowInfo(bool show_children)
	{
		logti("%s", GetInfoString(show_children).CStr());
	}

	bool ApplicationMetrics::OnStreamCreated(const info::Stream &stream)
	{
		std::unique_lock<std::shared_mutex> lock(_streams_guard);

		// If already stream metrics is exist,
		if (_streams.find(stream.GetId()) != _streams.end())
		{
			return true;
		}

		auto stream_metrics = std::make_shared<StreamMetrics>(GetSharedPtr(), stream);
		if (stream_metrics == nullptr)
		{
			logte("Cannot create StreamMetrics (%s/%s - %s)", GetVHostAppName().CStr(), stream.GetName().CStr(), stream.GetUUID().CStr());
			return false;
		}

		_streams[stream.GetId()] = stream_metrics;

		logti("Create StreamMetrics(%s/%s) for monitoring", stream.GetName().CStr(), stream.GetUUID().CStr());
		return true;
	}

	bool ApplicationMetrics::OnStreamDeleted(const info::Stream &stream)
	{
		auto stream_metric = GetStreamMetrics(stream);
		if (stream_metric == nullptr)
		{
			return false;
		}

		logti("Delete StreamMetrics(%s/%s) for monitoring", stream.GetName().CStr(), stream.GetUUID().CStr());

		// logging StreamMetric
		stream_metric->ShowInfo();

		std::unique_lock<std::shared_mutex> lock(_streams_guard);
		{
			_streams.erase(stream.GetId());
		}

		return true;
	}

	std::map<uint32_t, std::shared_ptr<StreamMetrics>> ApplicationMetrics::GetStreamMetricsMap()
	{
		std::shared_lock<std::shared_mutex> lock(_streams_guard);
		return _streams;
	}

	std::shared_ptr<StreamMetrics> ApplicationMetrics::GetStreamMetrics(info::stream_id_t stream_id)
	{
		std::shared_lock<std::shared_mutex> lock(_streams_guard);
		if (_streams.find(stream_id) == _streams.end())
		{
			return nullptr;
		}

		return _streams[stream_id];
	}

	bool ApplicationMetrics::OnStreamReserved(ProviderType who, const ov::Url &stream_uri, const ov::String &stream_name)
	{
		auto reserved_stream_metric = std::make_shared<ReservedStreamMetrics>(who, stream_uri, stream_name);

		std::lock_guard<std::shared_mutex> lock(_reserved_streams_guard);
		_reserved_streams[stream_uri.Port()] = reserved_stream_metric;

		logti("%s has reserved %s stream linked to %s", StringFromProviderType(who).CStr(), stream_name.CStr(), stream_uri.ToUrlString().CStr());

		return true;
	}

	std::map<uint32_t, std::shared_ptr<ReservedStreamMetrics>> ApplicationMetrics::GetReservedStreamMetricsMap()
	{
		std::shared_lock<std::shared_mutex> lock(_reserved_streams_guard);
		return _reserved_streams;
	}

	std::map<uint32_t, std::shared_ptr<ReservedStreamMetrics>> ApplicationMetrics::GetReservedStreamMetricsMap() const
	{
		std::shared_lock<std::shared_mutex> lock(_reserved_streams_guard);
		return _reserved_streams;
	}
}  // namespace mon