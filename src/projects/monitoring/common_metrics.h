//
// Created by getroot on 20. 1. 31.
//

#pragma once

#include "base/common_types.h"
#include "base/info/info.h"
#include "base/info/stream.h"

namespace mon
{
	class CommonMetrics
	{
	public:
		virtual ov::String GetInfoString(bool show_children = true);
		virtual void ShowInfo(bool show_children = true);

		uint32_t GetUnusedTimeSec() const;
		const std::chrono::system_clock::time_point &GetCreatedTime() const;
		const std::chrono::system_clock::time_point &GetLastUpdatedTime() const;

		virtual uint64_t GetTotalBytesIn() const;
		virtual uint64_t GetTotalBytesOut() const;
		virtual uint64_t GetAvgThroughputIn() const;
		virtual uint64_t GetAvgThroughputOut() const;
		virtual uint64_t GetMaxThroughputIn() const;
		virtual uint64_t GetMaxThroughputOut() const;
		virtual uint64_t GetLastThroughputIn() const;
		virtual uint64_t GetLastThroughputOut() const;
		virtual uint32_t GetTotalConnections() const;
		virtual uint32_t GetMaxTotalConnections() const;
		virtual std::chrono::system_clock::time_point GetMaxTotalConnectionsTime() const;
		virtual std::chrono::system_clock::time_point GetLastRecvTime() const;
		virtual std::chrono::system_clock::time_point GetLastSentTime() const;
		virtual uint64_t GetBytesOut(PublisherType type) const;
		virtual uint64_t GetConnections(PublisherType type) const;
		uint32_t GetModuleUsageCount(cmn::MediaCodecModuleId module_id) const;

		virtual void IncreaseBytesIn(uint64_t value);
		virtual void IncreaseBytesOut(PublisherType type, uint64_t value);
		void IncreaseModuleUsageCount(cmn::MediaCodecModuleId module_id);
		void DecreaseModuleUsageCount(cmn::MediaCodecModuleId module_id);
		virtual void OnSessionConnected(PublisherType type);
		virtual void OnSessionDisconnected(PublisherType type);
		virtual void OnSessionsDisconnected(PublisherType type, uint64_t number_of_sessions);

	protected:
		CommonMetrics();
		~CommonMetrics() = default;

		// Renew last updated time
		void UpdateDate();
		void UpdateThroughput();

		std::chrono::system_clock::time_point _created_time;
		std::chrono::system_clock::time_point _last_updated_time;

		// From Provider
		std::atomic<uint64_t> _total_bytes_in;

		// From Publishers
		std::atomic<uint64_t> _total_bytes_out;

		std::atomic<uint32_t> _total_connections;
		std::atomic<uint32_t> _max_total_connections;
		// Time to reach maximum number of connections.
		// TODO(Getroot): Does it need mutex? Check!
		std::chrono::system_clock::time_point _max_total_connection_time;
		std::chrono::system_clock::time_point _last_recv_time;
		std::chrono::system_clock::time_point _last_sent_time;

		// Throughput from Provider
		std::atomic<uint64_t> _avg_throughtput_in;
		std::atomic<uint64_t> _max_throughtput_in;
		std::atomic<uint64_t> _last_throughtput_in;
		std::atomic<uint64_t> _last_total_bytes_in;

		// Throughput from Publishers
		std::atomic<uint64_t> _avg_throughtput_out;
		std::atomic<uint64_t> _max_throughtput_out;
		std::atomic<uint64_t> _last_throughtput_out;
		std::atomic<uint64_t> _last_total_bytes_out;

		std::atomic<int64_t> _last_throughput_measure_time;

		// From Publishers
		class PublisherMetrics
		{
		public:
			std::atomic<uint64_t> _bytes_out;
			std::atomic<uint32_t> _connections;
		};

		PublisherMetrics _publisher_metrics[static_cast<int8_t>(PublisherType::NumberOfPublishers)];
		std::atomic<int32_t> _module_usage_count[ov::ToUnderlyingType(cmn::MediaCodecModuleId::NB)] = {};
	};
}  // namespace mon