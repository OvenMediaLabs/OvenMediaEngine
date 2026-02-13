//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2019 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <condition_variable>
#include <optional>
#include <queue>
#include <shared_mutex>

#include "./dump_utilities.h"
#include "./log.h"
#include "./ovdata_structure.h"
#include "./stop_watch.h"
#include "./string.h"

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

			auto shared_lock = std::shared_lock(_name_mutex);
			logt("ov.Queue", "[%p] %s is created with threshold: %zu, interval: %d", this, _queue_name.CStr(), threshold, log_interval_in_msec);
		}

		~Queue()
		{
			auto shared_lock = std::shared_lock(_name_mutex);
			logt("ov.Queue", "[%p] %s is destroyed", this, _queue_name.CStr());
		}

		String GetAlias() const
		{
			auto shared_lock = std::shared_lock(_name_mutex);
			return _queue_name;
		}

		void SetAlias(const char *alias)
		{
			auto lock_guard = std::lock_guard(_name_mutex);

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
			auto lock_guard = std::lock_guard(_name_mutex);

			_threshold = threshold;
			logt("ov.Queue", "[%p] The threshold is changed to %d", this, _threshold);
		}

		void Enqueue(const T &item)
		{
			auto lock_guard = std::lock_guard(_mutex);

			_queue.push(item);

			CheckThreshold();

			_condition.notify_all();
		}

		void Enqueue(T &&item)
		{
			auto lock_guard = std::lock_guard(_mutex);

			_queue.push(std::move(item));

			CheckThreshold();

			_condition.notify_all();
		}

		// Timeout in milliseconds
		std::optional<T> Front(int timeout = Infinite)
		{
			auto unique_lock = std::unique_lock(_mutex);

			if (_stop == false)
			{
				// If there is data in the queue, return immediately without condition wait
				auto result = (_queue.empty() == false) ? true : false;
				if (!result)
				{
					std::chrono::system_clock::time_point expire =
						(timeout == Infinite) ? std::chrono::system_clock::time_point::max() : std::chrono::system_clock::now() + std::chrono::milliseconds(timeout);

					result = _condition.wait_until(unique_lock, expire, [this]() -> bool {
						return ((_queue.empty() == false) || _stop);
					});
				}
				if (result)
				{
					if (_stop == false)
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
			auto unique_lock = std::unique_lock(_mutex);

			if (_stop == false)
			{
				// If there is data in the queue, return immediately without condition wait
				auto result = (_queue.empty() == false) ? true : false;

				if (result == false)
				{
					std::chrono::system_clock::time_point expire =
						(timeout == Infinite) ? std::chrono::system_clock::time_point::max() : std::chrono::system_clock::now() + std::chrono::milliseconds(timeout);

					result = _condition.wait_until(unique_lock, expire, [this]() -> bool {
						return ((_queue.empty() == false) || _stop);
					});
				}

				if (result)
				{
					if (_stop == false)
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
			auto unique_lock = std::unique_lock(_mutex);

			if (_stop == false)
			{
				// If there is data in the queue, return immediately without condition wait
				auto result = (_queue.empty() == false) ? true : false;

				if (result == false)
				{
					if (timeout > 0)
					{
						std::chrono::system_clock::time_point expire =
							(timeout == Infinite) ? std::chrono::system_clock::time_point::max() : std::chrono::system_clock::now() + std::chrono::milliseconds(timeout);

						result = _condition.wait_until(unique_lock, expire, [this]() -> bool {
							return ((_queue.empty() == false) || _stop);
						});
					}
					else
					{
						// Do not wait if timeout is 0 or negative
					}
				}

				if (result)
				{
					if (_stop == false)
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
			auto lock_guard = std::lock_guard(_mutex);

			return _queue.empty();
		}

		void Clear()
		{
			auto lock_guard = std::lock_guard(_mutex);

			// empty the queue
			_queue = {};
		}

		size_t Size() const
		{
			auto lock_guard = std::lock_guard(_mutex);

			return _queue.size();
		}

		bool IsStopped() const
		{
			return _stop;
		}

		void Start()
		{
			auto lock_guard = std::lock_guard(_mutex);

			_stop = false;
		}

		void Stop()
		{
			auto lock_guard = std::lock_guard(_mutex);

			_stop = true;
			_condition.notify_all();
		}

	protected:
		inline void CheckThreshold()
		{
			if (_peak < _queue.size())
			{
				_peak = _queue.size();
			}

			if ((_threshold > 0) && (_queue.size() >= _threshold))
			{
				if (_last_log_time.IsElapsed(_log_interval) && _last_log_time.Update())
				{
					auto shared_lock = std::shared_lock(_name_mutex);
					logw("ov.Queue", "[%p] %s size has exceeded the threshold: queue: %zu, threshold: %zu, peak: %zu", this, _queue_name.CStr(), _queue.size(), _threshold, _peak);
				}
			}
		}

	private:
		StopWatch _last_log_time;

		std::shared_mutex _name_mutex;
		String _queue_name;

		size_t _threshold = 0;
		size_t _peak = 0;
		int _log_interval = 0;

		std::queue<T> _queue;
		mutable std::mutex _mutex;
		std::condition_variable _condition;
		bool _stop = false;
	};

}  // namespace ov
