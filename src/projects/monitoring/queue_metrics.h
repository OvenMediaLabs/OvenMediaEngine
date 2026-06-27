//
// Created by getroot on 20. 11. 25.
//

#pragma once

#include <mutex>
#include <shared_mutex>

#include "base/info/managed_queue.h"

namespace mon
{
	class QueueMetrics
	{
	public:
		QueueMetrics(const info::ManagedQueue &info)
			: _id(info.GetId()),
			  _urn(info.GetUrn()),
			  _type_name(info.GetTypeName()),
			  _threshold(info.GetThreshold()),
			  _peak(0),
			  _size(0),
			  _input_message_per_second(0),
			  _output_message_per_second(0),
			  _drop_count(0),
			  _waiting_time(0)
		{
		}

		~QueueMetrics()
		{
		}

		uint32_t GetId() const
		{
			return _id;
		}

		// Returns by value: the caller must hold its own reference because the monitor
		// thread may reassign _urn concurrently (see UpdateMetadata).
		std::shared_ptr<info::ManagedQueue::URN> GetUrn() const
		{
			std::shared_lock lock(_mutex);
			return _urn;
		}

		ov::String GetTypeName() const
		{
			std::shared_lock lock(_mutex);
			return _type_name;
		}

		void UpdateMetadata(const info::ManagedQueue &info)
		{
			std::unique_lock lock(_mutex);
			_urn	   = info.GetUrn();
			_type_name = info.GetTypeName();
		}

		void UpdateMetrics(const info::ManagedQueue &info)
		{
			std::unique_lock lock(_mutex);
			_peak					   = info.GetPeak();
			_size					   = info.GetSize();
			_threshold				   = info.GetThreshold();
			_input_message_per_second  = info.GetInputMessagePerSecond();
			_output_message_per_second = info.GetOutputMessagePerSecond();
			_drop_count				   = info.GetDropCount();
			_waiting_time			   = info.GetWaitingTimeInUs();
		}

		size_t GetPeak() const
		{
			std::shared_lock lock(_mutex);
			return _peak;
		}

		size_t GetSize() const
		{
			std::shared_lock lock(_mutex);
			return _size;
		}

		size_t GetThreshold() const
		{
			std::shared_lock lock(_mutex);
			return _threshold;
		}

		size_t GetInputMessagePerSecond() const
		{
			std::shared_lock lock(_mutex);
			return _input_message_per_second;
		}

		size_t GetOutputMessagePerSecond() const
		{
			std::shared_lock lock(_mutex);
			return _output_message_per_second;
		}

		size_t GetDropCount() const
		{
			std::shared_lock lock(_mutex);
			return _drop_count;
		}

		int64_t GetWaitingTime() const
		{
			std::shared_lock lock(_mutex);
			return _waiting_time;
		}

	private:
		mutable std::shared_mutex _mutex;

		// metadata
		uint32_t _id;
		std::shared_ptr<info::ManagedQueue::URN> _urn;
		ov::String _type_name;

		// metrics
		size_t _threshold;
		size_t _peak;
		size_t _size;
		size_t _input_message_per_second;
		size_t _output_message_per_second;
		size_t _drop_count;
		int64_t _waiting_time;
	};
}  // namespace mon