//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Keukhan Kwon
//  Copyright (c) 2019 AirenSoft. All rights reserved.
//
//==============================================================================

#pragma once

#include <monitoring/monitoring.h>

#include <condition_variable>
#include <optional>
#include <queue>
#include <shared_mutex>

#include "base/info/managed_queue.h"
#include "base/ovlibrary/ovlibrary.h"

#define MANAGED_QUEUE_METRICS_UPDATE_INTERVAL_IN_MSEC 1000
#define MANAGED_QUEUE_LOG_INTERVAL_IN_MSEC 5000


namespace ov
{
	template <typename T>
	class ManagedQueue : public info::ManagedQueue
	{
	private:
		const char* LOG_TAG = "ManagedQueue";

		struct ManagedQueueNode
		{
			T data;

			ManagedQueueNode* next;

			std::chrono::high_resolution_clock::time_point _start;
			bool _urgent = false;

			ManagedQueueNode(const T& value, bool urgent, ManagedQueueNode* next_node = nullptr)
				: data(value), next(next_node), _start(std::chrono::high_resolution_clock::time_point::min()), _urgent(urgent)
			{
				_start = std::chrono::high_resolution_clock::now();
			}
		};

	public:
		ManagedQueue()
			: ManagedQueue(nullptr) {}

		ManagedQueue(std::shared_ptr<info::ManagedQueue::URN> urn, size_t threshold = 0, int log_interval_in_msec = MANAGED_QUEUE_LOG_INTERVAL_IN_MSEC)
			: info::ManagedQueue(threshold),
			  _stats_metric_interval(MANAGED_QUEUE_METRICS_UPDATE_INTERVAL_IN_MSEC),
			  _log_interval(log_interval_in_msec),
			  _front_node(nullptr),
			  _rear_node(nullptr),
			  _stop(false)
		{
			info::ManagedQueue::SetUrn(urn, Demangle(typeid(T).name()).CStr());

			// Register to the server metrics
			// If the Unique id is duplicated or memory allocation failed, retry
			while (true)
			{
				SetId(IssueUniqueQueueId());

				if (MonitorInstance->OnQueueCreated(*this) == true)
				{
					break;
				}
			}
		}

		~ManagedQueue()
		{
			Clear();

			// Unregister to the server metrics
			MonitorInstance->OnQueueDeleted(*this);
		}

		void SetUrn(std::shared_ptr<info::ManagedQueue::URN> urn)
		{
			info::ManagedQueue::SetUrn(urn, Demangle(typeid(T).name()).CStr());

			MonitorInstance->OnQueueUpdated(*this, true);
		}

		void SetThreshold(size_t threshold)
		{
			info::ManagedQueue::SetThreshold(threshold);

			MonitorInstance->OnQueueUpdated(*this);
		}

		// Set threshold in time-base mode.
		// The effective item count is estimated as: input_message_per_second * time_ms / 1000.
		void SetThresholdByTime(int time_ms)
		{
			info::ManagedQueue::SetThresholdByTime(time_ms);

			MonitorInstance->OnQueueUpdated(*this);
		}				

		// Urgent item will be inserted at the front of the queue
		void Enqueue(const T& item, bool urgent = false, int timeout = Infinite)
		{
			auto node = new ManagedQueueNode(item, urgent);
			EnqeuePos pos = urgent ? EnqeuePos::EnqueuFrontPos : EnqeuePos::EnqueuBackPos;

			EnqueueInternal(node, timeout, pos);
		}

		// Urgent item will be inserted at the front of the queue
		void Enqueue(T&& item, bool urgent = false, int timeout = Infinite)
		{
			auto node = new ManagedQueueNode(item, urgent);
			EnqeuePos pos = urgent ? EnqeuePos::EnqueuFrontPos : EnqeuePos::EnqueuBackPos;

			EnqueueInternal(node, timeout, pos);
		}

		std::optional<T> Front(int timeout = Infinite)
		{
			auto unique_lock = std::unique_lock(_mutex);

			if (_stop)
			{
				return {};	// Stop is requested
			}

			if (_size == 0)
			{
				std::chrono::system_clock::time_point expire = (timeout == Infinite) ? std::chrono::system_clock::time_point::max() : std::chrono::system_clock::now() + std::chrono::milliseconds(timeout);

				auto result = _condition.wait_until(unique_lock, expire, [this]() -> bool {
					return (((_size == 0) == false) || _stop);
				});

				if (!result || _stop)
				{
					return {};	// timed out / Stop is requested
				}
			}

			return _front_node->data;
		}

		// How long the first message has been buffered
		int32_t GetBufferedTimeMs()
		{
			auto lock_guard = std::lock_guard(_mutex);

			return GetBufferedTimeMsInternal();
		}

		std::optional<T> Back(int timeout = Infinite)
		{
			auto unique_lock = std::unique_lock(_mutex);

			if (_stop)
			{
				return {};	// Stop is requested
			}

			if (_size == 0)
			{
				std::chrono::system_clock::time_point expire = (timeout == Infinite) ? std::chrono::system_clock::time_point::max() : std::chrono::system_clock::now() + std::chrono::milliseconds(timeout);

				auto result = _condition.wait_until(unique_lock, expire, [this]() -> bool {
					return (((_size == 0) == false) || _stop);
				});

				if (!result || _stop)
				{
					return {};	// timed out / Stop is requested
				}
			}

			return _rear_node->data;
		}

		std::optional<T> Dequeue(int timeout = Infinite)
		{
			auto unique_lock = std::unique_lock(_mutex);

			if (_stop)
			{
				return {};	// Stop is requested
			}

			// Compute the hard deadline from the caller-supplied timeout.
			auto deadline = (timeout != Infinite)
				? std::chrono::system_clock::now() + std::chrono::milliseconds(timeout)
				: std::chrono::system_clock::time_point::max();

			// Wait loop: expire is recalculated on every wakeup so that the
			// buffering_delay timer fires correctly even when no new packets
			// arrive after the stream ends (which would otherwise leave the
			// queue stuck with expire == time_point::max()).
			while (!_stop)
			{
				// Check whether an item is ready to be dequeued.
				if (_buffering_delay == 0)
				{
					if (_size != 0) 
					{
						break;
					}
				}
				else
				{
					if (_front_node != nullptr && _front_node->_urgent == true) 
					{
						break;
					}
					if (GetBufferedTimeMsInternal() >= _buffering_delay) 
					{
						break;
					}
				}

				// Compute the next wakeup time: whichever comes first between
				// the caller deadline and the buffering-ready time.
				auto expire = deadline;
				if (_buffering_delay != 0 && _front_node != nullptr)
				{
					int remaining_ms = _buffering_delay - GetBufferedTimeMsInternal();
					if (remaining_ms <= 0)
					{
						// Time elapsed between the check above and here — ready now.
						continue;
					}
					auto buffering_expire = std::chrono::system_clock::now() + std::chrono::milliseconds(remaining_ms);
					if (buffering_expire < expire)
					{
						expire = buffering_expire;
					}
				}

				_condition.wait_until(unique_lock, expire);

				// Check the hard deadline after waking.
				if (timeout != Infinite && std::chrono::system_clock::now() >= deadline)
				{
					return {};	// timed out
				}
			}

			if (_stop)
			{
				return {};	// Stop is requested
			}

			ManagedQueueNode* node = _front_node;
			_front_node = _front_node->next;
			if (_front_node == nullptr)
			{
				_rear_node = nullptr;
			}

			T value = std::move(node->data);

			_size--;

			// Update statistics of output message count
			_output_message_count++;

			// Update statistics of waiting time (microseconds)
			if (node->_start != std::chrono::system_clock::time_point::max())
			{
				auto current = std::chrono::high_resolution_clock::now();
				_waiting_time_in_us = _waiting_time_in_us * 0.9 + std::chrono::duration_cast<std::chrono::microseconds>(current - node->_start).count() * 0.1;
			}

			delete node;

			UpdateMetrics();

			if(_exceed_threshold_and_wait_enabled == true)
			{
				_condition.notify_all();
			}

			return value;
		}

		bool IsEmpty() const
		{
			auto lock_guard = std::lock_guard(_mutex);

			return (_size == 0);
		}

		// Cleared all items in the queue
		void Clear()
		{
			auto lock_guard = std::lock_guard(_mutex);

			while (_front_node != nullptr)
			{
				ManagedQueueNode* temp = _front_node;

				_front_node = _front_node->next;

				delete temp;
			}

			_rear_node = nullptr;

			_size = 0;

			ClearMetrics();
		}

		size_t Size() const
		{
			auto lock_guard = std::lock_guard(_mutex);

			return _size;
		}

		void Stop()
		{
			auto lock_guard = std::lock_guard(_mutex);

			_stop = true;

			ClearMetrics();

			_condition.notify_all();
		}

		bool IsStopped() const
		{
			return _stop;
		}

		void SetExceedWaitEnable(bool enable)
		{
			_exceed_threshold_and_wait_enabled = enable;
		}

		bool IsExceedWaitEnable()
		{
			return _exceed_threshold_and_wait_enabled;
		}

		// Buffer keeps items for a certain amount of time
		void SetBufferingDelay(int delay_ms)
		{
			_buffering_delay = delay_ms;
		}

		ov::String GetInfoString()
		{
			auto shared_lock = std::shared_lock(_name_mutex);

			return ov::String::FormatString(
				"ManagedQueue [Id: %u, Size: %zu, Threshold: %zu (%s %ld%s + delay %dms), Peak: %zu, Imps: %zu, Omps: %zu, Wait: %s, Urn: %s]",
				GetId(), _size, _threshold,
				GetThresholdModeString(), _threshold_value, (_threshold_mode == ThresholdMode::TimeBased) ? " ms" : "", _buffering_delay,
				_peak, _input_message_per_second, _output_message_per_second, _exceed_threshold_and_wait_enabled ? "On" : "Off",
				_urn->ToString().CStr());
		}

	private:

		int32_t GetBufferedTimeMsInternal()
		{
			if (_front_node == nullptr)
			{
				return 0;
			}

			if (_front_node->_urgent == true)
			{
				return ov::Infinite;
			}

			auto current = std::chrono::high_resolution_clock::now();
			return std::chrono::duration_cast<std::chrono::milliseconds>(current - _front_node->_start).count();
		}

		// timeout works when the queue is full and _exceed_threshold_and_wait_enabled is true
		// If the queue is full, it waits until the queue is less than the threshold or the timeout expires.
		// If the timeout expires, the message is dropped.
		// This is to avoid dropping items when the queue size exceeds the threshold, if it exceeds it momentarily due to jitter.
		enum EnqeuePos : int8_t
		{
			EnqueuFrontPos = 0,
			EnqueuBackPos
		};

		void EnqueueInternal(ManagedQueueNode* node, int timeout, EnqeuePos push_method)
		{
			auto unique_lock = std::unique_lock(_mutex);			

			if (!node)
			{
				logc(LOG_TAG, "Failed to allocate memory. id:%u", GetId());
				return;
			}

			// Update statistics of input message count
			_input_message_count++;

			// Wait until the queue size is less than threshold
			if(_exceed_threshold_and_wait_enabled == true)
			{
				std::chrono::system_clock::time_point expire = (timeout == Infinite) ? std::chrono::system_clock::time_point::max() : std::chrono::system_clock::now() + std::chrono::milliseconds(timeout);
				auto result = _condition.wait_until(unique_lock, expire, [this]() -> bool {
					return (!IsThresholdExceeded());
				});
				if (!result || _stop)
				{
					loge(LOG_TAG, "[%s] queue is full. q.size(%zu), q.threshold(%zu)", _urn->ToString().CStr(), _size, _threshold);
					delete node;
					return;
				}
			}

			if (push_method == EnqeuePos::EnqueuBackPos)
			{
				PushBack(node);
			}
			else
			{
				PushFront(node);
			}

			UpdateMetrics();

			// Always notify: when buffering_delay == 0 this is the normal signal;
			// when buffering_delay != 0 this lets the Dequeue thread recalculate
			// its timed wait based on the (possibly new) front node.
			_condition.notify_all();
		}

		void PushBack(ManagedQueueNode* node)
		{
			if (_size == 0)
			{
				_front_node = node;
			}
			else
			{
				_rear_node->next = node;
			}

			_rear_node = node;

			_size++;
		}

		void PushFront(ManagedQueueNode* node)
		{
			if (_size == 0)
			{
				_rear_node = node;
			}
			else
			{
				node->next = _front_node;
			}

			_front_node = node;

			_size++;
		}

	protected:
		// Update statistical metrics and send data to monitoring module.
		void UpdateMetrics()
		{
			// Update the peak statistics
			if (_peak < _size)
			{
				_peak = _size;
			}

			if (_timer.IsStart() == false)
			{
				_timer.Start();
			}

			if (_timer.IsElapsed(_stats_metric_interval))
			{
				int elapsed_time = _timer.Elapsed();
				_timer.Update();

				// Update statistics of message per second
				_input_message_per_second = (double)(_input_message_count - _last_input_message_count) * (1000.0 / (double)elapsed_time);
				_output_message_per_second = (double)(_output_message_count - _last_output_message_count) * (1000.0 / (double)elapsed_time);
				_last_input_message_count = _input_message_count;
				_last_output_message_count = _output_message_count;
	
				UpdateThreshold();

				if (IsThresholdExceeded())
				{
					_threshold_exceeded_time_in_us += _stats_metric_interval;

					// Logging
					_last_logging_time += _stats_metric_interval;
					if ((_last_logging_time >= _log_interval) && (_last_logged_peak < _peak))
					{
						_last_logging_time = 0;

						auto shared_lock = std::shared_lock(_name_mutex);
						logw(LOG_TAG, "Exceeded. %s", GetInfoString().CStr());

						_last_logged_peak = _peak;
					}
				}
				else
				{
					_threshold_exceeded_time_in_us = 0;

					logt(LOG_TAG, "Stable. %s", GetInfoString().CStr());
				}

				MonitorInstance->OnQueueUpdated(*this);
			}
		}

		void ClearMetrics()
		{
			_peak = 0;
			_input_message_per_second = 0;
			_output_message_per_second = 0;
			_input_message_count = 0;
			_output_message_count = 0;
			_threshold_exceeded_time_in_us = 0;

			MonitorInstance->OnQueueUpdated(*this);
		}

	private:

		// Check if the queue has exceeded the threshold.
		// _threshold == 0 means no threshold.
		bool IsThresholdExceeded() const
		{
			if (_threshold == 0) return false;
			return _size >= _threshold;
		}

		// Compute the threshold
		void UpdateThreshold()
		{
			// Compute the delay buffer count 
			size_t delay_buffer_count = 0;
			if (_buffering_delay > 0 && _input_message_per_second > 0)
			{
				delay_buffer_count = static_cast<size_t>(static_cast<double>(_input_message_per_second) * (static_cast<double>(_buffering_delay) / 1000.0));
			}

			// For time-based threshold, compute the effective count from the input message rate + delay buffer
			if (_threshold_mode == ThresholdMode::TimeBased && _input_message_per_second > 0)
			{
				size_t base_count = std::max(static_cast<size_t>(1), static_cast<size_t>(static_cast<double>(_input_message_per_second) * (static_cast<double>(_threshold_value) / 1000.0)));
				_threshold = base_count + delay_buffer_count;
			}
			// For count-based threshold + delay buffer
			else if (_threshold_mode == ThresholdMode::CountBased && _threshold_value > 0)
			{
				_threshold = static_cast<size_t>(_threshold_value) + delay_buffer_count;
			}
		}

		StopWatch _timer;

		int _stats_metric_interval = 0;

		int _log_interval = 0;
		int64_t _last_logging_time = 0;

		// Linked list of the queue
		ManagedQueueNode* _front_node;
		ManagedQueueNode* _rear_node;

		// Mutex and condition variable for the queue
		mutable std::mutex _mutex;
		std::condition_variable _condition;

		// Stop flag
		bool _stop;

		// Use to print logs when the peak value of the queue is increased.
		size_t _last_logged_peak = 0;

		// Prevent exceed threshold. If true, the queue will not exceed the threshold
		// Wait until the queue falls below the threshold
		bool _exceed_threshold_and_wait_enabled = false;
	};

}  // namespace ov