//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2019 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <atomic>
#include <optional>
#include <queue>

#include "./dump_utilities.h"
#include "./log.h"
#include "./ovdata_structure.h"
#include "./stop_watch.h"
#include "./string.h"
#include "./tsa/mutex.h"

namespace ov
{
	template <typename T>
	class Queue
	{
	public:
		Queue()
			: Queue(nullptr)
		{
		}

		Queue(const char *alias, size_t threshold = 0, int log_interval_in_msec = 5000)
			: _threshold(threshold),
			  _log_interval(log_interval_in_msec)
		{
			SetAlias(alias);

			_last_log_time.Start();

			SharedLockGuard shared_lock(_name_mutex);
			logt("ov.Queue", "[%p] %s is created with threshold: %zu, interval: %d", this, _queue_name.CStr(), threshold, log_interval_in_msec);
		}

		~Queue()
		{
			SharedLockGuard shared_lock(_name_mutex);
			logt("ov.Queue", "[%p] %s is destroyed", this, _queue_name.CStr());
		}

		String GetAlias() const
		{
			SharedLockGuard shared_lock(_name_mutex);
			return _queue_name;
		}

		void SetAlias(const char *alias)
		{
			LockGuard lock_guard(_name_mutex);

			if ((alias != nullptr) && (alias[0] != '\0'))
			{
				_queue_name = alias;
			}
			else
			{
				_queue_name.Format("Queue<%s>", Demangle(typeid(T).name()).CStr());
			}

			logt("ov.Queue", "[%p] The alias is changed to %s", this, _queue_name.CStr());
		}

		void SetThreshold(size_t threshold)
		{
			// `_threshold` is published with `memory_order_relaxed`,
			// so the new value becomes effective from a subsequent `Enqueue` (eventual visibility).
			// Used for the threshold log, where immediate effect is not required.
			_threshold.store(threshold, std::memory_order_relaxed);
			logt("ov.Queue", "[%p] The threshold is set to %zu (applied to subsequent Enqueue calls)", this, threshold);
		}

		void Enqueue(const T &item)
		{
			LockGuard lock_guard(_mutex);

			_queue.push(item);

			CheckThreshold();

			_condition.NotifyAll();
		}

		void Enqueue(T &&item)
		{
			LockGuard lock_guard(_mutex);

			_queue.push(std::move(item));

			CheckThreshold();

			_condition.NotifyAll();
		}

		// Timeout in milliseconds
		std::optional<T> Front(int timeout = Infinite)
		{
			LockGuard unique_lock(_mutex);

			if (_stop.load(std::memory_order_relaxed) == false)
			{
				// If there is data in the queue, return immediately without condition wait
				auto result = (_queue.empty() == false) ? true : false;
				if (!result)
				{
					std::chrono::steady_clock::time_point expire =
						(timeout == Infinite) ? std::chrono::steady_clock::time_point::max() : std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout);

					result = _condition.WaitUntil(unique_lock, expire, [this]() OV_REQUIRES(_mutex) -> bool {
						return ((_queue.empty() == false) || _stop.load(std::memory_order_relaxed));
					});
				}
				if (result)
				{
					if (_stop.load(std::memory_order_relaxed) == false)
					{
						return _queue.front();
					}
					else
					{
						// Stop is requested
					}
				}
				else
				{
					// timed out
				}
			}
			else
			{
				// Stop is requested
			}

			return {};
		}

		// Timeout in milliseconds
		std::optional<T> Back(int timeout = Infinite)
		{
			LockGuard unique_lock(_mutex);

			if (_stop.load(std::memory_order_relaxed) == false)
			{
				// If there is data in the queue, return immediately without condition wait
				auto result = (_queue.empty() == false) ? true : false;

				if (result == false)
				{
					std::chrono::steady_clock::time_point expire =
						(timeout == Infinite) ? std::chrono::steady_clock::time_point::max() : std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout);

					result = _condition.WaitUntil(unique_lock, expire, [this]() OV_REQUIRES(_mutex) -> bool {
						return ((_queue.empty() == false) || _stop.load(std::memory_order_relaxed));
					});
				}

				if (result)
				{
					if (_stop.load(std::memory_order_relaxed) == false)
					{
						return _queue.back();
					}
					else
					{
						// Stop is requested
					}
				}
				else
				{
					// timed out
				}
			}
			else
			{
				// Stop is requested
			}

			return {};
		}

		// Timeout in milliseconds
		std::optional<T> Dequeue(int timeout = Infinite)
		{
			LockGuard unique_lock(_mutex);

			if (_stop.load(std::memory_order_relaxed) == false)
			{
				// If there is data in the queue, return immediately without condition wait
				auto result = (_queue.empty() == false) ? true : false;

				if (result == false)
				{
					if (timeout > 0)
					{
						std::chrono::steady_clock::time_point expire =
							(timeout == Infinite) ? std::chrono::steady_clock::time_point::max() : std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout);

						result = _condition.WaitUntil(unique_lock, expire, [this]() OV_REQUIRES(_mutex) -> bool {
							return ((_queue.empty() == false) || _stop.load(std::memory_order_relaxed));
						});
					}
					else
					{
						// Do not wait if timeout is 0 or negative
					}
				}

				if (result)
				{
					if (_stop.load(std::memory_order_relaxed) == false)
					{
						T value = std::move(_queue.front());
						_queue.pop();

						return value;
					}
					else
					{
						// Stop is requested
					}
				}
				else
				{
					// timed out
				}
			}
			else
			{
				// Stop is requested
			}

			return std::nullopt;
		}

		bool IsEmpty() const
		{
			LockGuard lock_guard(_mutex);

			return _queue.empty();
		}

		void Clear()
		{
			LockGuard lock_guard(_mutex);

			// empty the queue
			_queue = {};
		}

		size_t Size() const
		{
			LockGuard lock_guard(_mutex);

			return _queue.size();
		}

		bool IsStopped() const
		{
			return _stop.load(std::memory_order_relaxed);
		}

		void Start()
		{
			LockGuard lock_guard(_mutex);

			_stop.store(false, std::memory_order_relaxed);
		}

		void Stop()
		{
			LockGuard lock_guard(_mutex);

			_stop.store(true, std::memory_order_relaxed);
			_condition.NotifyAll();
		}

	protected:
		inline void CheckThreshold() OV_REQUIRES(_mutex)
		{
			if (_peak < _queue.size())
			{
				_peak = _queue.size();
			}

			const auto threshold = _threshold.load(std::memory_order_relaxed);
			if ((threshold > 0) && (_queue.size() >= threshold))
			{
				if (_last_log_time.IsElapsed(_log_interval) && _last_log_time.Update())
				{
					SharedLockGuard shared_lock(_name_mutex);
					logw("ov.Queue", "[%p] %s size has exceeded the threshold: queue: %zu, threshold: %zu, peak: %zu", this, _queue_name.CStr(), _queue.size(), threshold, _peak);
				}
			}
		}

	private:
		StopWatch _last_log_time;

		mutable SharedMutex _name_mutex;
		String _queue_name OV_GUARDED_BY(_name_mutex);

		// Self-only atomic:
		//
		// This member's modification order is the only thing the atomic guarantees;
		// it is never used to publish other shared state.
		// All access sites use `memory_order_relaxed`,
		// which is sufficient because cross-data visibility (queue contents, alias, etc.)
		// is provided by `_mutex`/`_name_mutex` at higher level.
		// If a future change adds a "publish via threshold" pattern,
		// switch to acquire/release here AND on every read site.
		std::atomic<size_t> _threshold = 0;
		size_t _peak OV_GUARDED_BY(_mutex) = 0;
		int _log_interval = 0;

		std::queue<T> _queue OV_GUARDED_BY(_mutex);
		mutable Mutex _mutex;
		ConditionVariable _condition;
		// Self-only atomic:
		//
		// Same contract as `_threshold` above.
		// Writes happen under `_mutex` and pair with `_condition.NotifyAll()` for waiter wakeup;
		// the lock itself supplies happens-before, so the atomic stores/loads can stay relaxed.
		// `IsStopped()` reads without the lock and tolerates "eventual visibility" of
		// the latest value - acceptable for polling, not for cross-data publish.
		std::atomic<bool> _stop = false;
	};

}  // namespace ov
