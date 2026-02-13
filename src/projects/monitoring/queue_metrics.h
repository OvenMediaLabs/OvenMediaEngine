//
// Created by getroot on 20. 11. 25.
//

#pragma once

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

		const uint32_t &GetId() const
		{
			return _id;
		}

		const std::shared_ptr<info::ManagedQueue::URN> &GetUrn() const
		{
			return _urn;
		}

		const ov::String &GetTypeName() const
		{
			return _type_name;
		}

		void UpdateMetadata(const info::ManagedQueue &info)
		{
			_urn	   = info.GetUrn();
			_type_name = info.GetTypeName();
			_threshold = info.GetThreshold();
		}

		void UpdateMetrics(const info::ManagedQueue &info)
		{
			_peak					   = info.GetPeak();
			_size					   = info.GetSize();
			_input_message_per_second  = info.GetInputMessagePerSecond();
			_output_message_per_second = info.GetOutputMessagePerSecond();
			_drop_count				   = info.GetDropCount();
			_waiting_time			   = info.GetWaitingTimeInUs();
		}

		const size_t &GetPeak() const
		{
			return _peak;
		}

		const size_t &GetSize() const
		{
			return _size;
		}

		const size_t &GetThreshold() const
		{
			return _threshold;
		}

		const size_t &GetInputMessagePerSecond() const
		{
			return _input_message_per_second;
		}

		const size_t &GetOutputMessagePerSecond() const
		{
			return _output_message_per_second;
		}

		const size_t &GetDropCount() const
		{
			return _drop_count;
		}

		const int64_t &GetWaitingTime() const
		{
			return _waiting_time;
		}

	private:
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