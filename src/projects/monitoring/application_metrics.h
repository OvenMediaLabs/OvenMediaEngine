//
// Created by getroot on 20. 1. 16.
//
#pragma once

#include <shared_mutex>

#include "base/common_types.h"
#include "base/info/info.h"
#include "common_metrics.h"
#include "reserved_stream_metrics.h"
#include "stream_metrics.h"

namespace mon
{
	class HostMetrics;
	class ApplicationMetrics : public info::Application, public CommonMetrics, public ov::EnableSharedFromThis<ApplicationMetrics>
	{
		const char *GetApplicationTypeName() final
		{
			return "ApplicationMetrics";
		}

	public:
		ApplicationMetrics(const std::shared_ptr<HostMetrics> &host_metrics, const info::Application &app_info)
			: info::Application(app_info),
			  _host_metrics(host_metrics)
		{
		}

		~ApplicationMetrics()
		{
			_host_metrics.reset();

			Release();
		}

		void Release()
		{
			std::unique_lock<std::shared_mutex> lock(_streams_guard);
			_streams.clear();
		}

		std::shared_ptr<HostMetrics> GetHostMetrics()
		{
			return _host_metrics;
		}

		ov::String GetInfoString(bool show_children = true) override;
		void ShowInfo(bool show_children = true) override;

		bool OnStreamCreated(const info::Stream &stream);
		bool OnStreamDeleted(const info::Stream &stream);
		std::map<uint32_t, std::shared_ptr<StreamMetrics>> GetStreamMetricsMap();
		std::shared_ptr<StreamMetrics> GetStreamMetrics(info::stream_id_t stream_id);
		std::shared_ptr<StreamMetrics> GetStreamMetrics(const info::Stream &stream)
		{
			return GetStreamMetrics(stream.GetId());
		}

		bool OnStreamReserved(ProviderType who, const ov::Url &stream_uri, const ov::String &stream_name);
		bool OnStreamCanceled(const ov::Url &stream_uri);  // Reservation Canceled
		std::map<uint32_t, std::shared_ptr<ReservedStreamMetrics>> GetReservedStreamMetricsMap();
		// TODO: To prevent side-effects from occurring, the value returned must also be changed to const
		// For example: std::map<uint32_t, std::shared_ptr<const ReservedStreamMetrics>>
		std::map<uint32_t, std::shared_ptr<ReservedStreamMetrics>> GetReservedStreamMetricsMap() const;

	private:
		std::shared_ptr<HostMetrics> _host_metrics;
		std::shared_mutex _streams_guard;
		std::map<uint32_t, std::shared_ptr<StreamMetrics>> _streams;

		mutable std::shared_mutex _reserved_streams_guard;
		std::map<uint32_t, std::shared_ptr<ReservedStreamMetrics>> _reserved_streams;
	};
}  // namespace mon
