//
// Created by getroot on 20. 1. 31.
//
#include "common_metrics.h"

#include "monitoring_private.h"

namespace mon
{
#define THROUGHPUT_MEASURE_INTERVAL_MS 1000 // 1s

	CommonMetrics::CommonMetrics()
	{
		_total_bytes_in				  = 0;
		_total_bytes_out			  = 0;
		_total_connections			  = 0;
		_max_total_connections		  = 0;

		_avg_throughtput_in			  = 0;
		_max_throughtput_in			  = 0;
		_last_throughtput_in		  = 0;
		_last_total_bytes_in		  = 0;

		_avg_throughtput_out		  = 0;
		_max_throughtput_out		  = 0;
		_last_throughtput_out		  = 0;
		_last_total_bytes_out		  = 0;

		_last_throughput_measure_time = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
		_max_total_connection_time	  = std::chrono::system_clock::now();
		_last_recv_time				  = std::chrono::system_clock::now();
		_last_sent_time				  = std::chrono::system_clock::now();

		for (int i = 0; i < static_cast<int8_t>(PublisherType::NumberOfPublishers); i++)
		{
			_publisher_metrics[i]._bytes_out   = 0;
			_publisher_metrics[i]._connections = 0;
		}
		_created_time = std::chrono::system_clock::now();
		UpdateDate();
	}

	ov::String CommonMetrics::GetInfoString([[maybe_unused]] bool show_children)
	{
		ov::String out_str;

		out_str.AppendFormat(
			"\n\t>> Statistics\n"
			"\tLast update time : %s, Last sent time : %s\n"
			"\tBytes in : %s, Bytes out : %s, Concurrent connections : %u, Max connections : %u (%s)\n",
			ov::Converter::ToString(GetLastUpdatedTime()).CStr(), ov::Converter::ToString(GetLastSentTime()).CStr(),
			ov::Converter::BytesToString(GetTotalBytesIn()).CStr(), ov::Converter::BytesToString(GetTotalBytesOut()).CStr(), GetTotalConnections(),
			GetMaxTotalConnections(), ov::Converter::ToString(GetMaxTotalConnectionsTime()).CStr());

		out_str.AppendFormat("\n\t\t>>>> By publisher\n");
		for (int i = 0; i < static_cast<int8_t>(PublisherType::NumberOfPublishers); i++)
		{
			// Deprecated Statistics
			if (
				static_cast<PublisherType>(i) == PublisherType::Unknown ||
				static_cast<PublisherType>(i) == PublisherType::RtmpPush ||
				static_cast<PublisherType>(i) == PublisherType::MpegtsPush ||
				static_cast<PublisherType>(i) == PublisherType::SrtPush)
			{
				continue;
			}

			out_str.AppendFormat("\t\t- %s : Bytes out(%s) Concurrent Connections (%" PRIu64 ")\n",
								 ::StringFromPublisherType(static_cast<PublisherType>(i)).CStr(),
								 ov::Converter::BytesToString(GetBytesOut(static_cast<PublisherType>(i))).CStr(),
								 GetConnections(static_cast<PublisherType>(i)));
		}

		return out_str;
	}

	void CommonMetrics::ShowInfo([[maybe_unused]] bool show_children)
	{
		logti("%s", GetInfoString().CStr());
	}

	uint32_t CommonMetrics::GetUnusedTimeSec() const
	{
		auto current = std::chrono::high_resolution_clock::now();
		return std::chrono::duration_cast<std::chrono::seconds>(current - GetLastUpdatedTime()).count();
	}

	const std::chrono::system_clock::time_point &CommonMetrics::GetCreatedTime() const
	{
		return _created_time;
	}

	const std::chrono::system_clock::time_point &CommonMetrics::GetLastUpdatedTime() const
	{
		return _last_updated_time;
	}

	uint64_t CommonMetrics::GetTotalBytesIn() const
	{
		return _total_bytes_in;
	}
	uint64_t CommonMetrics::GetTotalBytesOut() const
	{
		return _total_bytes_out;
	}

	uint64_t CommonMetrics::GetAvgThroughputIn() const
	{
		return _avg_throughtput_in;
	}

	uint64_t CommonMetrics::GetAvgThroughputOut() const
	{
		return _avg_throughtput_out;
	}

	uint64_t CommonMetrics::GetMaxThroughputIn() const
	{
		return _max_throughtput_in;
	}

	uint64_t CommonMetrics::GetMaxThroughputOut() const
	{
		return _max_throughtput_out;
	}

	uint64_t CommonMetrics::GetLastThroughputIn() const
	{
		return _last_throughtput_in;
	}

	uint64_t CommonMetrics::GetLastThroughputOut() const
	{
		return _last_throughtput_out;
	}

	uint32_t CommonMetrics::GetTotalConnections() const
	{
		return _total_connections;
	}

	uint32_t CommonMetrics::GetMaxTotalConnections() const
	{
		return _max_total_connections;
	}
	std::chrono::system_clock::time_point CommonMetrics::GetMaxTotalConnectionsTime() const
	{
		return _max_total_connection_time;
	}

	std::chrono::system_clock::time_point CommonMetrics::GetLastRecvTime() const
	{
		return _last_recv_time;
	}

	std::chrono::system_clock::time_point CommonMetrics::GetLastSentTime() const
	{
		return _last_sent_time;
	}

	uint64_t CommonMetrics::GetBytesOut(PublisherType type) const
	{
		return _publisher_metrics[static_cast<int8_t>(type)]._bytes_out;
	}
	uint64_t CommonMetrics::GetConnections(PublisherType type) const
	{
		return _publisher_metrics[static_cast<int8_t>(type)]._connections;
	}

	uint32_t CommonMetrics::GetModuleUsageCount(cmn::MediaCodecModuleId module_id) const
	{
		auto count = _module_usage_count[ov::ToUnderlyingType(module_id)].load();

		// In theory, due to a race condition, there may be temporary occurrences of negative value,
		// so this is adjusted accordingly.
		return std::max(count, 0);
	}

	void CommonMetrics::IncreaseBytesIn(uint64_t value)
	{
		_total_bytes_in += value;
		_last_recv_time = std::chrono::system_clock::now();

		// If there are no clients of the publisher, output throughput is not calculated.
		// So, In/Oout throughput calculations are handled here.
		UpdateThroughput();

		UpdateDate();
	}

	void CommonMetrics::IncreaseBytesOut(PublisherType type, uint64_t value)
	{
		if (value == 0)
		{
			return;
		}

		_publisher_metrics[static_cast<int8_t>(type)]._bytes_out += value;
		_total_bytes_out += value;
		_last_sent_time = std::chrono::system_clock::now();

		UpdateDate();
	}

	void CommonMetrics::IncreaseModuleUsageCount(cmn::MediaCodecModuleId module_id)
	{
		_module_usage_count[ov::ToUnderlyingType(module_id)]++;
	}

	void CommonMetrics::DecreaseModuleUsageCount(cmn::MediaCodecModuleId module_id)
	{
		_module_usage_count[ov::ToUnderlyingType(module_id)]--;
	}

	void CommonMetrics::OnSessionConnected(PublisherType type)
	{
		_publisher_metrics[static_cast<int8_t>(type)]._connections++;
		_total_connections++;

		if (_total_connections.load() > _max_total_connections.load())
		{
			_max_total_connections.exchange(_total_connections);
			_max_total_connection_time = std::chrono::system_clock::now();
		}

		UpdateDate();
	}
	void CommonMetrics::OnSessionDisconnected(PublisherType type)
	{
		_publisher_metrics[static_cast<int8_t>(type)]._connections--;
		_total_connections--;

		UpdateDate();
	}

	void CommonMetrics::OnSessionsDisconnected(PublisherType type, uint64_t number_of_sessions)
	{
		_publisher_metrics[static_cast<int8_t>(type)]._connections -= number_of_sessions;
		_total_connections -= number_of_sessions;

		UpdateDate();
	}

	// Renew last updated time
	void CommonMetrics::UpdateDate()
	{
		_last_updated_time = std::chrono::system_clock::now();
	}

	void CommonMetrics::UpdateThroughput()
	{
		auto now_ms	 = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
		auto last_ms = _last_throughput_measure_time.load();
		// Bug fix: use compare_exchange_strong to prevent multiple threads from updating simultaneously
		if ((now_ms - last_ms) > THROUGHPUT_MEASURE_INTERVAL_MS && _last_throughput_measure_time.compare_exchange_strong(last_ms, now_ms))
		{
			int32_t interval_sec = THROUGHPUT_MEASURE_INTERVAL_MS / 1000;

			// Calculate last second throughput of provider
			_last_throughtput_in		  = (_total_bytes_in.load() - _last_total_bytes_in.load());

			// Calculate average throughput of provider
			_avg_throughtput_in			  = (_total_bytes_in.load() - _last_total_bytes_in.load()) * 8 / interval_sec;
			if (_avg_throughtput_in.load() > _max_throughtput_in.load())
			{
				_max_throughtput_in.store(_avg_throughtput_in);
			}
			_last_total_bytes_in.store(_total_bytes_in);

			// Calculate last second throughput of publisher
			_last_throughtput_out = (_total_bytes_out.load() - _last_total_bytes_out.load());

			// Calculate average throughput of publisher
			_avg_throughtput_out  = (_total_bytes_out.load() - _last_total_bytes_out.load()) * 8 / interval_sec;
			if (_avg_throughtput_out.load() > _max_throughtput_out.load())
			{
				_max_throughtput_out.store(_avg_throughtput_out);
			}
			_last_total_bytes_out.store(_total_bytes_out);

			logt("CommonMetrics", "Throughput updated. In: %s/s In(Avg): %s/s, Out: %s/s Out(Avg): %s/s",
				 ov::Converter::BitToString(_last_throughtput_in.load()).CStr(), ov::Converter::BitToString(_avg_throughtput_in.load()).CStr(),
				 ov::Converter::BitToString(_last_throughtput_out.load()).CStr(), ov::Converter::BitToString(_avg_throughtput_out.load()).CStr());
		}
	}
}  // namespace mon