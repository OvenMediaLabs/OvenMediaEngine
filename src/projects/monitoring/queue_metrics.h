//
// Created by getroot on 20. 11. 25.
//

#pragma once

#include <base/ovlibrary/ovlibrary.h>

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
			ov::SharedLockGuard lock(_mutex);
			return _urn;
		}

		ov::String GetTypeName() const
		{
			ov::SharedLockGuard lock(_mutex);
			return _type_name;
		}

		void UpdateMetadata(const info::ManagedQueue &info)
		{
			// Read from info first (its getters take ManagedQueue's own lock); then take
			// _mutex only for the assignment to keep the exclusive section minimal and
			// avoid nested locking.
			auto urn	   = info.GetUrn();
			auto type_name = info.GetTypeName();

			ov::LockGuard lock(_mutex);
			_urn	   = std::move(urn);
			_type_name = std::move(type_name);
		}

		void UpdateMetrics(const info::ManagedQueue &info)
		{
			const auto peak						 = info.GetPeak();
			const auto size						 = info.GetSize();
			const auto threshold				 = info.GetThreshold();
			const auto input_message_per_second	 = info.GetInputMessagePerSecond();
			const auto output_message_per_second = info.GetOutputMessagePerSecond();
			const auto drop_count				 = info.GetDropCount();
			const auto waiting_time				 = info.GetWaitingTimeInUs();

			ov::LockGuard lock(_mutex);
			_peak					   = peak;
			_size					   = size;
			_threshold				   = threshold;
			_input_message_per_second  = input_message_per_second;
			_output_message_per_second = output_message_per_second;
			_drop_count				   = drop_count;
			_waiting_time			   = waiting_time;
		}

		size_t GetPeak() const
		{
			ov::SharedLockGuard lock(_mutex);
			return _peak;
		}

		size_t GetSize() const
		{
			ov::SharedLockGuard lock(_mutex);
			return _size;
		}

		size_t GetThreshold() const
		{
			ov::SharedLockGuard lock(_mutex);
			return _threshold;
		}

		size_t GetInputMessagePerSecond() const
		{
			ov::SharedLockGuard lock(_mutex);
			return _input_message_per_second;
		}

		size_t GetOutputMessagePerSecond() const
		{
			ov::SharedLockGuard lock(_mutex);
			return _output_message_per_second;
		}

		size_t GetDropCount() const
		{
			ov::SharedLockGuard lock(_mutex);
			return _drop_count;
		}

		int64_t GetWaitingTime() const
		{
			ov::SharedLockGuard lock(_mutex);
			return _waiting_time;
		}

	private:
		mutable ov::SharedMutex _mutex;

		// metadata
		uint32_t _id;
		std::shared_ptr<info::ManagedQueue::URN> _urn OV_GUARDED_BY(_mutex);
		ov::String _type_name OV_GUARDED_BY(_mutex);

		// metrics
		size_t _threshold OV_GUARDED_BY(_mutex);
		size_t _peak OV_GUARDED_BY(_mutex);
		size_t _size OV_GUARDED_BY(_mutex);
		size_t _input_message_per_second OV_GUARDED_BY(_mutex);
		size_t _output_message_per_second OV_GUARDED_BY(_mutex);
		size_t _drop_count OV_GUARDED_BY(_mutex);
		int64_t _waiting_time OV_GUARDED_BY(_mutex);
	};
}  // namespace mon
