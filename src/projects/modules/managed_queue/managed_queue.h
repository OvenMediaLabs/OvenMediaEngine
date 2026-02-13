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

// Deactivated as it is no longer used
#define SKIP_MESSAGE_ENABLED false
#define SKIP_MESSAGE_CHECK_INTERVAL 500					 // 0.5 sec
#define SKIP_MESSAGE_STABLE_FOR_RETRIEVE_INTERVAL 10000	 // 10 sec
#define SKIP_MESSAGE_LOG_INTERVAL 5000					 // 1 sec

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
			  _stop(false),
			  _skip_message_enabled(false)
		{
			info::ManagedQueue::SetUrn(urn, Demangle(typeid(T).name()).CStr());

			// Register to the server metrics
			// If the Unique id is duplicated or memory allocation failed, retry
			while (true)
			{
				SetId(IssueUniqueQueueId());

				if (MonitorInstance->GetServerMetrics()->OnQueueCreated(*this) == true)
				{
					break;
				}
			}
		}

		~ManagedQueue()
		{
			Clear();

			// Unregister to the server metrics
			MonitorInstance->GetServerMetrics()->OnQueueDeleted(*this);
		}

		void SetUrn(std::shared_ptr<info::ManagedQueue::URN> urn)
		{
			info::ManagedQueue::SetUrn(urn, Demangle(typeid(T).name()).CStr());

			MonitorInstance->GetServerMetrics()->OnQueueUpdated(*this, true);
		}

		void SetThreshold(size_t threshold)
		{
			info::ManagedQueue::SetThreshold(threshold);

			MonitorInstance->GetServerMetrics()->OnQueueUpdated(*this, true);
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

			if ((_buffering_delay == 0 &&_size == 0) || 
				(_buffering_delay != 0 && GetBufferedTimeMsInternal() < _buffering_delay))
			{
				std::chrono::system_clock::time_point expire = (timeout == Infinite) ? std::chrono::system_clock::time_point::max() : std::chrono::system_clock::now() + std::chrono::milliseconds(timeout);

				auto result = _condition.wait_until(unique_lock, expire, [this]() -> bool {
					if (_stop)
					{
						return true;
					}
					
					if (_buffering_delay == 0)
					{
						return (_size != 0);
					}
					else
					{
						if (_front_node != nullptr && _front_node->_urgent == true)
						{
							return true;
						}

						if (GetBufferedTimeMsInternal() >= _buffering_delay)
						{
							return true;
						}
					}

					return false;
				});

				if (!result || _stop)
				{
					return {};	// timed out / Stop is requested
				}
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

		// Skip message for performance failure recovery
		// If the queue size increases due to insufficient performance, the problem is avoided by dropping the message.
		// * Warning: This should only be used where dropping a message will not affect the operation. example) video encoder, video scaler
		void SetSkipMessageEnable(bool enable)
		{
			_skip_message_enabled = enable;
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

#if SKIP_MESSAGE_ENABLED
			if (_skip_message_enabled == true)
			{
				auto curr_time = ov::Time::GetTimestampInMs();
				auto elapsed_check_time = curr_time - _skip_messages_last_check_time;
				auto elapsed_stable_time = curr_time - _skip_messages_last_changed_time;

				if (elapsed_check_time > SKIP_MESSAGE_CHECK_INTERVAL)
				{
					_skip_messages_last_check_time = curr_time;

					if ((_size > _threshold / 10) &&				   // It exceeded 10% of the threshold
						(_size >= _skip_message_previous_queue_size))  // The queue size has increased compared to before
					{
						if (_skip_message_count < 10)  // Maximum skip count is 10
						{
							_skip_message_count++;
						}

						_skip_message_previous_queue_size = _size;
						_skip_messages_last_changed_time = curr_time;

						auto shared_lock = std::shared_lock(_name_mutex);
						logw(LOG_TAG, "[%s] Managed queue is unstable. q.size(%d), q.threshold(%d) q.imps(%d), q.omps(%d), skip(%d)", _urn->ToString().CStr(),
							 GetSize(), GetThreshold(), _input_message_per_second, _output_message_per_second, _skip_message_count);
					}
					// If the queue is stable, slowly decrease the number of skip frames.
					else if ((_skip_message_count > 0) &&										   // Skip Message is operating
							 (elapsed_stable_time > SKIP_MESSAGE_STABLE_FOR_RETRIEVE_INTERVAL) &&  // It appears to be in a stable state.
							 (_size <= 1))														   // The queue size is 1 or less.
					{
						if (--_skip_message_count < 0)
						{
							_skip_message_count = 0;
						}

						_skip_message_previous_queue_size = _size;
						_skip_messages_last_changed_time = curr_time;

						auto shared_lock = std::shared_lock(_name_mutex);
						logi(LOG_TAG, "[%s] Managed queue is stable. q.size(%d), q.threshold(%d) q.imps(%d), q.omps(%d), skip(%d)", _urn->ToString().CStr(),
							 GetSize(), GetThreshold(), _input_message_per_second, _output_message_per_second, _skip_message_count);
					}
				}

				if ((_skip_message_count > 0) && (_input_message_count % (_skip_message_count + 1) != 0))
				{
					if (curr_time - _skip_messages_last_log_time > SKIP_MESSAGE_LOG_INTERVAL)
					{
						_skip_messages_last_log_time = curr_time;

						auto shared_lock = std::shared_lock(_name_mutex);
						logw(LOG_TAG, "[%s] Drop a message by message skip. q.size(%d), q.threshold(%d) q.imps(%d), q.omps(%d), skip(%d)", _urn->ToString().CStr(),
							 GetSize(), GetThreshold(), _input_message_per_second, _output_message_per_second, _skip_message_count);
					}

					_drop_message_count++;

					delete node;

					_condition.notify_all();

					return;
				}

				// If the queue exceeds the threshold, drop the frame.
				if (IsThresholdExceeded() == true)
				{
					if (curr_time - _skip_messages_last_log_time > SKIP_MESSAGE_LOG_INTERVAL)
					{
						_skip_messages_last_log_time = curr_time;

						auto shared_lock = std::shared_lock(_name_mutex);
						logw(LOG_TAG, "[%s] Managed queue is exceed. drop message. q.size(%d), q.threshold(%d) q.imps(%d), q.omps(%d), skip(%d)", _urn->ToString().CStr(),
							 GetSize(), GetThreshold(), _input_message_per_second, _output_message_per_second, _skip_message_count);
					}

					_drop_message_count++;
					
					delete node;

					_condition.notify_all();

					return;
				}

				// logi(LOG_TAG, "q.size(%d), q.imps(%d), q.omps(%d), skip(%d), stable(%d)", _size,  _skip_message_count, elapsed_stable_time);
			}
#endif

			// Wait until the queue size is less than threshold
			if(_exceed_threshold_and_wait_enabled == true)
			{
				std::chrono::system_clock::time_point expire = (timeout == Infinite) ? std::chrono::system_clock::time_point::max() : std::chrono::system_clock::now() + std::chrono::milliseconds(timeout);
				auto result = _condition.wait_until(unique_lock, expire, [this]() -> bool {
					return (_size  < _threshold);
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

			if (_buffering_delay == 0)
			{
				_condition.notify_all();
			}
			else
			{
				// If the buffering delay is set, notify the waiting thread when the buffering time is exceeded.
				if (GetBufferedTimeMsInternal() >= _buffering_delay)
				{
					_condition.notify_all();
				}
			}
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
	
				int adjusted_size = _size;

				if (_buffering_delay > 0)
				{
					// excluding the estimated intended buffer size
					int intended_buffer_size = (double)_input_message_per_second * ((double)_buffering_delay / 1000.0);
					adjusted_size -= intended_buffer_size;

					// logi("DEBUG", "intended buffer size: %d, input_message_count: %d, input_message_per_second: %d, buffering_delay: %d, size: %d, adjusted_size: %d", intended_buffer_size, _input_message_count, _input_message_per_second, _buffering_delay, _size, adjusted_size);
					
					if (adjusted_size < 0)
					{
						adjusted_size = 0;
					}
				}

				if ((_threshold > 0) && ((size_t)adjusted_size >= _threshold))
				{
					_threshold_exceeded_time_in_us += _stats_metric_interval;

					// Logging
					_last_logging_time += _stats_metric_interval;
					if ((_last_logging_time >= _log_interval) && (_last_logged_peak < _peak))
					{
						_last_logging_time = 0;

						auto shared_lock = std::shared_lock(_name_mutex);
						logw(LOG_TAG, "[%u] %s has exceeded the threshold and increased peak. size: %zu, threshold: %zu, peak: %zu, imps: %zu, omps: %zu", GetId(), _urn->ToString().CStr(), _size, _threshold, _peak, _input_message_per_second, _output_message_per_second);

						_last_logged_peak = _peak;
					}
				}
				else
				{
					_threshold_exceeded_time_in_us = 0;
				}

				MonitorInstance->GetServerMetrics()->OnQueueUpdated(*this);
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

			MonitorInstance->GetServerMetrics()->OnQueueUpdated(*this);
		}

	private:
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

		// Message Skip for lack of performance
		bool _skip_message_enabled = false;
		int64_t _skip_messages_last_check_time = ov::Time::GetTimestampInMs();
		int64_t _skip_messages_last_changed_time = ov::Time::GetTimestampInMs();
		int64_t _skip_messages_last_log_time = 0;
		int32_t _skip_message_count = 0;
		size_t _skip_message_previous_queue_size = 0;

		// Prevent exceed threshold. If true, the queue will not exceed the threshold
		// Wait until the queue falls below the threshold
		bool _exceed_threshold_and_wait_enabled = false;

		// Delay
		int _buffering_delay = 0;
	};

}  // namespace ov