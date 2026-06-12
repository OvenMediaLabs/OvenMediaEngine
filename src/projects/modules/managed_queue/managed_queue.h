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

#include <atomic>
#include <optional>
#include <queue>

#include "base/info/managed_queue.h"
#include "base/ovlibrary/ovlibrary.h"

#define MANAGED_QUEUE_METRICS_UPDATE_INTERVAL_IN_MSEC 1000
#define MANAGED_QUEUE_LOG_INTERVAL_IN_MSEC 5000


namespace ov
{
	// Shutdown order: Stop() -> join threads -> delete queue.
	// Deleting the queue while a thread waits in Dequeue/Front/Back is UB.  The destructor calls Stop() just in case.
	template <typename T>
	class ManagedQueue : public info::ManagedQueue
	{
	private:
		const char* LOG_TAG = "ManagedQueue";

		struct ManagedQueueNode
		{
			T data;

			ManagedQueueNode* next;

			std::chrono::steady_clock::time_point _start;
			bool _urgent = false;

			ManagedQueueNode(T value, bool urgent, ManagedQueueNode *next_node = nullptr)
				: data(std::move(value)), next(next_node), _start(std::chrono::steady_clock::now()), _urgent(urgent) {}
		};

	public:
		ManagedQueue()
			: ManagedQueue(nullptr) {}

		ManagedQueue(std::shared_ptr<info::ManagedQueue::URN> urn, size_t threshold = 0, int log_interval_in_msec = MANAGED_QUEUE_LOG_INTERVAL_IN_MSEC)
			: info::ManagedQueue(threshold),
			  _stats_metric_interval(MANAGED_QUEUE_METRICS_UPDATE_INTERVAL_IN_MSEC),
			  _log_interval(log_interval_in_msec),
			  _front_node(nullptr),
			  _rear_node(nullptr)
		{
			info::ManagedQueue::SetUrn(urn, Demangle(typeid(T).name()).CStr());

			SetId(IssueUniqueQueueId());

			if (MonitorInstance->OnQueueCreated(*this) == false)
			{
				logw(LOG_TAG, "Failed to register queue to monitor. id:%u", GetId());
			}
		}

		~ManagedQueue()
		{
			// Defensive: wake any remaining waiters so they can exit
			Stop();

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
		void SetThresholdByTime(size_t time_ms)
		{
			info::ManagedQueue::SetThresholdByTime(time_ms);

			MonitorInstance->OnQueueUpdated(*this);
		}

		// Urgent item will be inserted at the front of the queue
		void Enqueue(T item, bool urgent = false, int timeout = Infinite)
		{
			auto node = new(std::nothrow) ManagedQueueNode(std::move(item), urgent);
			if (node == nullptr)
			{
				loge(LOG_TAG, "Failed to allocate memory for queue node.");
				return;
			}
			EnqueuePos pos = urgent ? EnqueuePos::EnqueueFrontPos : EnqueuePos::EnqueueBackPos;

			EnqueueInternal(node, timeout, pos);
		}

		std::optional<T> Front(int timeout = Infinite)
		{
			LockGuard lock(_mutex);

			if (_stop)
			{
				return {};	// Stop is requested
			}

			if (_size == 0)
			{
				std::chrono::steady_clock::time_point expire = (timeout == Infinite) ? std::chrono::steady_clock::time_point::max() : std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout);

				auto result = _condition.WaitUntil(lock, expire, [this]() OV_REQUIRES(_mutex) -> bool {
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
			LockGuard lock_guard(_mutex);

			return GetBufferedTimeMsInternal();
		}

		std::optional<T> Back(int timeout = Infinite)
		{
			LockGuard lock(_mutex);

			if (_stop)
			{
				return {};	// Stop is requested
			}

			if (_size == 0)
			{
				std::chrono::steady_clock::time_point expire = (timeout == Infinite) ? std::chrono::steady_clock::time_point::max() : std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout);

				auto result = _condition.WaitUntil(lock, expire, [this]() OV_REQUIRES(_mutex) -> bool {
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
			LockGuard lock(_mutex);

			if (_stop)
			{
				return {};	// Stop is requested
			}

			// Compute the hard deadline from the caller-supplied timeout.
			auto deadline = (timeout != Infinite)
				? std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout)
				: std::chrono::steady_clock::time_point::max();

			// Wait loop: expire is recalculated on every wakeup so that the
			// buffering_delay timer fires correctly even when no new packets
			// arrive after the stream ends (which would otherwise leave the
			// queue stuck with expire == time_point::max()).
			while (!_stop)
			{
				// Woken by InjectWakeup(): consume the flag and return nullopt so
				// the caller can re-evaluate its state. Checked before the
				// readiness checks so a stale flag never leaks into a later call.
				if (_pending_wakeup)
				{
					_pending_wakeup = false;
					return {};
				}

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
					auto buffering_expire = std::chrono::steady_clock::now() + std::chrono::milliseconds(remaining_ms);
					if (buffering_expire < expire)
					{
						expire = buffering_expire;
					}
				}

				_condition.WaitUntil(lock, expire);

				// Check the hard deadline after waking.
				if (timeout != Infinite && std::chrono::steady_clock::now() >= deadline)
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
			_waiting_time_in_us = _waiting_time_in_us * 0.9 + std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - node->_start).count() * 0.1;

			delete node;

			UpdateMetrics();

			if(_exceed_threshold_and_wait_enabled == true)
			{
				_condition.NotifyAll();
			}

			return value;
		}

		bool IsEmpty() const
		{
			LockGuard lock_guard(_mutex);

			return (_size == 0);
		}

		// Returns true if the queue has been over its threshold for at least `duration`.
		// Notes: exceeded time is updated only on each stats tick (MANAGED_QUEUE_METRICS_UPDATE_INTERVAL_IN_MSEC)
		bool IsThresholdExceededFor(std::chrono::milliseconds duration) const
		{
			return _threshold_exceeded_time_ms.load(std::memory_order_acquire) >= static_cast<int64_t>(duration.count());
		}

		// Cleared all items in the queue
		void Clear()
		{
			LockGuard lock_guard(_mutex);

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
			LockGuard lock_guard(_mutex);

			return _size;
		}

		void Stop()
		{
			LockGuard lock_guard(_mutex);

			_stop.store(true, std::memory_order_release);

			ClearMetrics();

			_condition.NotifyAll();
		}

		// Injects a one-shot wakeup so the consumer can re-evaluate its state.
		// Behaves as if an empty item were injected into the queue: the next
		// Dequeue() returns std::nullopt exactly once, even if the queue is
		// non-empty and even if no consumer is waiting (the wakeup is
		// remembered, never lost). Queued items are preserved. A consumer on a
		// queue that uses InjectWakeup() must treat std::nullopt as a
		// "re-evaluate state" signal, not as "queue empty".
		//
		// notify_all() keeps the wakeup reliable when Front()/Back() waiters coexist.
		void InjectWakeup()
		{
			LockGuard lock_guard(_mutex);

			_pending_wakeup = true;

			_condition.NotifyAll();
		}

		bool IsStopped() const
		{
			return _stop.load(std::memory_order_acquire);
		}

		void SetExceedWaitEnable(bool enable)
		{
			_exceed_threshold_and_wait_enabled.store(enable, std::memory_order_release);
		}

		bool IsExceedWaitEnable()
		{
			return _exceed_threshold_and_wait_enabled.load(std::memory_order_acquire);
		}

		// Buffer keeps items for a certain amount of time
		void SetBufferingDelay(int delay_ms)
		{
			LockGuard lock_guard(_mutex);
			if(delay_ms < 0)
			{
				logw(LOG_TAG, "[%s] Invalid buffering delay value: %d. Setting to 0.", GetInfoString().CStr(), delay_ms);
				delay_ms = 0;
			}

			_buffering_delay = delay_ms;
		}

		ov::String GetInfoString()
		{
			SharedLockGuard shared_lock(_name_mutex);

			ov::String urn_str = (_urn != nullptr) ? _urn->ToString() : ov::String("NoUrn");

			ov::String threshold_info = ov::String::FormatString("%zu (%s %zu%s", _threshold, GetThresholdModeString(_threshold_mode), _threshold_value, (_threshold_mode == ThresholdMode::TimeBased) ? "ms" : "");
			if(_buffering_delay > 0)
			{
				threshold_info.AppendFormat(" + delay %dms", _buffering_delay.load());
			}
			threshold_info.Append(")");

			return ov::String::FormatString(
				"ManagedQueue [Id: %u, Size: %zu, Threshold: %s, Peak: %zu, Imps: %zu, Omps: %zu, Wait: %s, Urn: %s]",
				GetId(), _size.load(), threshold_info.CStr(),
				_peak.load(), _input_message_per_second.load(), _output_message_per_second.load(), _exceed_threshold_and_wait_enabled ? "On" : "Off",
				urn_str.CStr());
		}

	private:

		int32_t GetBufferedTimeMsInternal() OV_REQUIRES(_mutex)
		{
			if (_front_node == nullptr)
			{
				return 0;
			}

			if (_front_node->_urgent == true)
			{
				return ov::Infinite;
			}

			auto current = std::chrono::steady_clock::now();
			return std::chrono::duration_cast<std::chrono::milliseconds>(current - _front_node->_start).count();
		}

		// timeout works when the queue is full and _exceed_threshold_and_wait_enabled is true
		// If the queue is full, it waits until the queue is less than the threshold or the timeout expires.
		// If the timeout expires, the message is dropped.
		// This is to avoid dropping items when the queue size exceeds the threshold, if it exceeds it momentarily due to jitter.
		enum EnqueuePos : int8_t
		{
			EnqueueFrontPos = 0,
			EnqueueBackPos
		};

		void EnqueueInternal(ManagedQueueNode* node, int timeout, EnqueuePos push_method)
		{
			LockGuard lock(_mutex);

			// Update statistics of input message count
			_input_message_count++;

			// Wait until the queue size is less than threshold
			if(_exceed_threshold_and_wait_enabled == true)
			{
				std::chrono::steady_clock::time_point expire = (timeout == Infinite) ? std::chrono::steady_clock::time_point::max() : std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout);
				auto result = _condition.WaitUntil(lock, expire, [this]() OV_REQUIRES(_mutex) -> bool {
					return (!IsThresholdExceeded() || _stop);
				});
				if (_stop)
				{
					{
						SharedLockGuard name_lock(_name_mutex);
						logw(LOG_TAG, "[%s] Stop is requested. Failed to enqueue item.", (_urn != nullptr) ? _urn->ToString().CStr() : "NoUrn");
					}
					delete node;
					return;
				}

				if (!result)
				{
					{
						SharedLockGuard name_lock(_name_mutex);
						loge(LOG_TAG, "[%s] queue is full. q.size(%zu), q.threshold(%zu)", (_urn != nullptr) ? _urn->ToString().CStr() : "NoUrn", _size.load(), _threshold);
					}
					delete node;
					return;
				}
			}

			if (push_method == EnqueuePos::EnqueueBackPos)
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
			_condition.NotifyAll();
		}

		void PushBack(ManagedQueueNode* node) OV_REQUIRES(_mutex)
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

		void PushFront(ManagedQueueNode* node) OV_REQUIRES(_mutex)
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
		void UpdateMetrics() OV_REQUIRES(_mutex)
		{
			// Update the peak statistics
			if (_peak < _size)
			{
				_peak = _size.load();
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
				_last_input_message_count = _input_message_count.load();
				_last_output_message_count = _output_message_count.load();
	
				UpdateThreshold();

				if (IsThresholdExceeded())
				{
					_threshold_exceeded_time_ms.fetch_add(elapsed_time);

					// Logging
					_last_logging_time += elapsed_time;
					if ((_last_logging_time >= _log_interval) && (_last_logged_peak < _peak))
					{
						_last_logging_time = 0;

						logw(LOG_TAG, "Exceeded. %s", GetInfoString().CStr());

						_last_logged_peak = _peak;
					}
				}
				else
				{
					_threshold_exceeded_time_ms.store(0);
#if DEBUG
					logt(LOG_TAG, "Stable. %s", GetInfoString().CStr());
#endif					
				}

				MonitorInstance->OnQueueUpdated(*this);
			}
		}

		void ClearMetrics() OV_REQUIRES(_mutex)
		{
			_peak = 0;
			_input_message_per_second = 0;
			_output_message_per_second = 0;
			_input_message_count = 0;
			_output_message_count = 0;

			_last_input_message_count = 0;
			_last_output_message_count = 0;
			_threshold_exceeded_time_ms.store(0);

			_last_logging_time = 0;
			_last_logged_peak = 0;

			_timer.Stop();

			MonitorInstance->OnQueueUpdated(*this);
		}

	private:
		// Check if the queue has exceeded the threshold.
		// _threshold == 0 means no threshold.
		bool IsThresholdExceeded() const OV_REQUIRES(_mutex)
		{
			SharedLockGuard shared_lock(_name_mutex);
			if (_threshold == 0) return false;
			return _size >= _threshold;
		}

		// Compute the threshold
		void UpdateThreshold() OV_REQUIRES(_mutex)
		{
			LockGuard name_lock(_name_mutex);

			// Compute the delay buffer count
			size_t delay_buffer_count = 0;
			if (_buffering_delay > 0 && _input_message_per_second > 0)
			{
				delay_buffer_count = static_cast<size_t>(static_cast<double>(_input_message_per_second) * (static_cast<double>(_buffering_delay) / 1000.0));
			}

			// For time-based threshold, compute the effective count from the input message rate + delay buffer
			if (_threshold_mode == ThresholdMode::TimeBased)
			{
				size_t base_count = 0;
				if (_threshold_value > 0 && _input_message_per_second > 0)
				{
					base_count = std::max(static_cast<size_t>(1), static_cast<size_t>(static_cast<double>(_input_message_per_second) * (static_cast<double>(_threshold_value) / 1000.0)));
				}
				_threshold = base_count + delay_buffer_count;
			}
			// For count-based threshold + delay buffer
			else if (_threshold_mode == ThresholdMode::CountBased)
			{
				size_t base_count = 0;
				if (_threshold_value > 0)
				{
					base_count = _threshold_value;
				}
				_threshold = base_count + delay_buffer_count;
			}
		}

		StopWatch _timer OV_GUARDED_BY(_mutex);

		int _stats_metric_interval = 0;

		int _log_interval = 0;
		int64_t _last_logging_time OV_GUARDED_BY(_mutex) = 0;

		// Linked list of the queue
		ManagedQueueNode* _front_node OV_GUARDED_BY(_mutex);
		ManagedQueueNode* _rear_node OV_GUARDED_BY(_mutex);

	protected:
		// `_mutex` is `protected` so derived classes can take the lock when calling
		// protected helpers like `UpdateMetrics()`/`ClearMetrics()` which are annotated
		// `OV_REQUIRES(_mutex)`. Capability scope must match the methods that require it.
		mutable Mutex _mutex;

	private:
		// `_condition` stays `private`: notify/wait are internal queue-thread coordination
		// that derived classes should not touch directly.
		ConditionVariable _condition;

		// Stop flag
		std::atomic<bool> _stop{false};

		// Set by InjectWakeup(), consumed by Dequeue(). A pending one-shot
		// wakeup. Unlike _stop, the queue stays usable.
		bool _pending_wakeup OV_GUARDED_BY(_mutex) = false;

		// Use to print logs when the peak value of the queue is increased.
		size_t _last_logged_peak OV_GUARDED_BY(_mutex) = 0;

		// Prevent exceed threshold. If true, the queue will not exceed the threshold
		// Wait until the queue falls below the threshold
		std::atomic<bool> _exceed_threshold_and_wait_enabled{false};
	};

}  // namespace ov