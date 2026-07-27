//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2026 AirenSoft. All rights reserved.
//
//==============================================================================
#include "./task_pool.h"

#include <config/config_manager.h>

#include <algorithm>

#define OV_LOG_TAG "TaskPool"

namespace ov
{
	TaskPool::TaskPool()
	{
	}

	TaskPool::~TaskPool()
	{
		Stop();
	}

	bool TaskPool::Initialize()
	{
		auto server_config = cfg::ConfigManager::GetInstance()->GetServer();
		if (server_config == nullptr)
		{
			logte("Could not read the server configuration");
			return false;
		}

		const auto &pool_config = server_config->GetModules().GetTaskPool();

		Config config;
		config.thread_count		 = static_cast<size_t>(std::max(pool_config.GetThreadCount(), 1));
		config.max_tasks		 = static_cast<size_t>(std::max(pool_config.GetMaxTasks(), 1));
		config.auto_scale		 = pool_config.IsAutoScaleEnabled();
		config.max_thread_count	 = static_cast<size_t>(std::max(pool_config.GetMaxThreadCount(), 1));
		config.idle_timeout_msec = std::max(pool_config.GetIdleTimeoutMs(), 0);

		Configure(config);

		return true;
	}

	void TaskPool::Configure(const Config &config)
	{
		std::lock_guard<std::mutex> lock(_mutex);

		_config = config;

		// A pool that cannot hold a task or run one is not a pool
		_config.thread_count = std::max<size_t>(_config.thread_count, 1);
		_config.max_tasks = std::max<size_t>(_config.max_tasks, 1);
		_config.max_thread_count = std::max(_config.max_thread_count, _config.thread_count);
		// A negative timeout would otherwise read as "keep the workers running"
		_config.idle_timeout_msec = std::max(_config.idle_timeout_msec, 0);

		logti("TaskPool is configured - threads: %zu (max: %zu, auto scale: %s), max tasks: %zu, idle timeout: %d ms",
			  _config.thread_count, _config.max_thread_count, _config.auto_scale ? "on" : "off",
			  _config.max_tasks, _config.idle_timeout_msec);
	}

	size_t TaskPool::AddWorkers(size_t count)
	{
		size_t added_count = 0;

		for (size_t index = 0; index < count; index++)
		{
			try
			{
				std::thread worker(&TaskPool::WorkerThreadProc, this);

				::pthread_setname_np(worker.native_handle(), ov::String::FormatString("TaskPool-%zu", _next_worker_index++).CStr());

				_workers.emplace(worker.get_id(), std::move(worker));

				added_count++;
			}
			catch (const std::system_error &e)
			{
				// Post() must not throw at a caller that only expects false back
				logte("Could not start a worker: %s", e.what());
				break;
			}
		}

		return added_count;
	}

	void TaskPool::JoinFinishedWorkers()
	{
		std::vector<std::thread> finished_workers;

		{
			std::lock_guard<std::mutex> lock(_mutex);
			finished_workers.swap(_finished_workers);
		}

		for (auto &worker : finished_workers)
		{
			if (worker.joinable())
			{
				worker.join();
			}
		}
	}

	void TaskPool::ReportRejection()
	{
		_rejected_count++;

		// A full queue rejects everything that follows, and that is exactly when the log
		// must not be flooded
		auto now = std::chrono::steady_clock::now();
		if ((now - _last_reject_log_time) < std::chrono::seconds(1))
		{
			return;
		}

		logte("Too many tasks are waiting (%zu) for %zu workers, so %zu task(s) have been rejected",
			  _task_queue.size(), _workers.size(), _rejected_count);

		_last_reject_log_time = now;
		_rejected_count		  = 0;
	}

	bool TaskPool::Post(Task task)
	{
		if (task == nullptr)
		{
			OV_ASSERT2(task != nullptr);
			return false;
		}

		// Workers that stopped themselves are reaped here, outside the lock, so that an idle
		// pool does not keep their thread objects around until it shuts down
		JoinFinishedWorkers();

		{
			std::lock_guard<std::mutex> lock(_mutex);

			if (_stopped)
			{
				return false;
			}

			// Workers exist only once there is work for them, so the pool is topped up to its
			// configured size whenever the ones already waiting cannot take this task right
			// away. Without this, a pool that lost workers to the idle timeout would queue a
			// burst behind the few workers that happened to stay busy.
			if (_workers.empty())
			{
				AddWorkers(_config.thread_count);
			}

			if (_workers.empty())
			{
				logte("No worker could be started, so the task is rejected");
				return false;
			}

			if (_task_queue.size() >= _config.max_tasks)
			{
				bool worker_added = _config.auto_scale && (_workers.size() < _config.max_thread_count);

				if (worker_added)
				{
					worker_added = (AddWorkers(1) > 0);
				}

				if (worker_added == false)
				{
					ReportRejection();
					return false;
				}

				logtw("Added a worker because %zu tasks are waiting (workers: %zu)", _task_queue.size(), _workers.size());
			}

			_task_queue.push(std::move(task));
		}

		_condition.notify_one();

		return true;
	}

	size_t TaskPool::GetPendingCount() const
	{
		std::lock_guard<std::mutex> lock(_mutex);

		return _task_queue.size();
	}

	size_t TaskPool::GetThreadCount() const
	{
		std::lock_guard<std::mutex> lock(_mutex);

		return _workers.size();
	}

	void TaskPool::Stop()
	{
		// Held for the whole call so that a second caller waits for the workers to be gone
		// instead of returning while the first one is still joining them
		std::lock_guard<std::mutex> stop_lock(_stop_mutex);

		std::map<std::thread::id, std::thread> workers;

		{
			std::lock_guard<std::mutex> lock(_mutex);

			if (_stopped)
			{
				return;
			}

			_stopped = true;

			// Dropped rather than run, because whatever they were going to use may already
			// be on its way down
			std::queue<Task> empty_queue;
			_task_queue.swap(empty_queue);

			workers.swap(_workers);
		}

		_condition.notify_all();

		for (auto &[thread_id, worker] : workers)
		{
			if (worker.joinable())
			{
				worker.join();
			}
		}

		JoinFinishedWorkers();
	}

	void TaskPool::WorkerThreadProc()
	{
		while (true)
		{
			Task task;

			{
				std::unique_lock<std::mutex> lock(_mutex);

				auto is_ready = [this]() {
					return _stopped || (_task_queue.empty() == false);
				};

				_idle_worker_count++;

				bool has_work = true;
				if (_config.idle_timeout_msec > 0)
				{
					has_work = _condition.wait_for(lock, std::chrono::milliseconds(_config.idle_timeout_msec), is_ready);
				}
				else
				{
					// No timeout means the workers stay until the pool stops
					_condition.wait(lock, is_ready);
				}

				_idle_worker_count--;

				if (_stopped)
				{
					break;
				}

				// A timed out wait reports the predicate as it stands once the lock is back,
				// so an empty result here means no task arrived while this worker gave up
				if (has_work == false)
				{
					// Idle for long enough. Its own thread cannot be joined from here, so it
					// is handed over for the next Post() or Stop() to join.
					auto iterator = _workers.find(std::this_thread::get_id());
					if (iterator != _workers.end())
					{
						_finished_workers.push_back(std::move(iterator->second));
						_workers.erase(iterator);
					}

					break;
				}

				task = std::move(_task_queue.front());
				_task_queue.pop();
			}

			// A task of one module must not be able to take down a worker the others share
			try
			{
				task();
			}
			catch (const std::exception &e)
			{
				logte("A task has thrown an exception: %s", e.what());
			}
			catch (...)
			{
				logte("A task has thrown an unknown exception");
			}
		}
	}
}  // namespace ov
