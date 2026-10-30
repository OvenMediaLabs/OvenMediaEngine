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

		auto thread_count = pool_config.GetThreadCount();
		auto max_tasks	  = pool_config.GetMaxTasks();

		// Checked before the cast, because a negative value would turn into a huge one
		Config config;
		config.thread_count = static_cast<size_t>((thread_count > 0) ? thread_count : 1);
		config.max_tasks	= static_cast<size_t>((max_tasks > 0) ? max_tasks : 1);

		Configure(config);

		return true;
	}

	void TaskPool::Configure(const Config &config)
	{
		std::lock_guard<std::mutex> lock(_mutex);

		_config = config;

		if (_config.thread_count == 0)
		{
			_config.thread_count = 1;
		}

		if (_config.max_tasks == 0)
		{
			_config.max_tasks = 1;
		}

		logti("TaskPool is configured - threads: %zu, max tasks: %zu", _config.thread_count, _config.max_tasks);
	}

	size_t TaskPool::AddWorkers(size_t count)
	{
		size_t added_count = 0;

		for (size_t index = 0; index < count; index++)
		{
			try
			{
				std::thread worker(&TaskPool::WorkerThreadProc, this);

				::pthread_setname_np(worker.native_handle(), ov::String::FormatString("TaskPool-%zu", _workers.size()).CStr());

				_workers.push_back(std::move(worker));

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

	void TaskPool::ReportRejection()
	{
		_rejected_count++;

		auto now = std::chrono::steady_clock::now();
		if ((now - _last_reject_log_time) < std::chrono::seconds(1))
		{
			return;
		}

		logte("The task queue is full (MaxTasks %zu) with %zu workers, so %zu task(s) have been rejected. Raise ThreadCount or MaxTasks under <Modules><TaskPool> if this keeps happening.",
			  _config.max_tasks, _workers.size(), _rejected_count);

		_last_reject_log_time = now;
		_rejected_count		  = 0;
	}

	bool TaskPool::StartWorkersOnFirstTask()
	{
		if (_workers.empty())
		{
			AddWorkers(_config.thread_count);
		}

		if (_workers.empty())
		{
			logte("No worker could be started, so the task is rejected");
			return false;
		}

		return true;
	}

	bool TaskPool::CanAccept(Priority priority, std::optional<size_t> max_tasks)
	{
		if (_stopped)
		{
			return false;
		}

		if (StartWorkersOnFirstTask() == false)
		{
			return false;
		}

		const auto &queue = GetQueue(priority);
		if (queue.ready.size() + queue.delayed.size() >= max_tasks.value_or(_config.max_tasks))
		{
			ReportRejection();
			return false;
		}

		return true;
	}

	TaskPool::PriorityQueue &TaskPool::GetQueue(Priority priority)
	{
		return _queues[static_cast<size_t>(priority)];
	}

	bool TaskPool::Post(Task task, Priority priority, std::optional<size_t> max_tasks)
	{
		if (task == nullptr)
		{
			OV_ASSERT2(task != nullptr);
			return false;
		}

		{
			std::lock_guard<std::mutex> lock(_mutex);

			if (CanAccept(priority, max_tasks) == false)
			{
				return false;
			}

			GetQueue(priority).ready.push(std::move(task));
		}

		_condition.notify_one();

		return true;
	}

	bool TaskPool::PostDelayed(int after_msec, Task task, Priority priority, std::optional<size_t> max_tasks)
	{
		if (task == nullptr)
		{
			OV_ASSERT2(task != nullptr);
			return false;
		}

		{
			std::lock_guard<std::mutex> lock(_mutex);

			if (CanAccept(priority, max_tasks) == false)
			{
				return false;
			}

			GetQueue(priority).delayed.emplace(std::chrono::steady_clock::now() + std::chrono::milliseconds(after_msec), std::move(task));
		}

		// A waiting worker takes up the earliest time again
		_condition.notify_one();

		return true;
	}

	bool TaskPool::PostDedicated(const ov::String &worker_name, Task task, bool keep_alive)
	{
		if (task == nullptr)
		{
			OV_ASSERT2(task != nullptr);
			return false;
		}

		{
			std::lock_guard<std::mutex> lock(_mutex);

			if (_stopped)
			{
				return false;
			}

			auto &slot = _dedicated_workers[worker_name];
			const bool created_now = (slot == nullptr);
			if (created_now)
			{
				slot = std::make_unique<DedicatedWorker>();
				slot->keep_alive = keep_alive;
			}

			if (slot->task_queue.size() >= _config.max_tasks)
			{
				ReportRejection();
				return false;
			}

			if (slot->running == false)
			{
				// The previous worker has left for lack of work; joined before a new one starts
				if (slot->thread.joinable())
				{
					slot->thread.join();
				}

				try
				{
					slot->thread = std::thread(&TaskPool::DedicatedWorkerThreadProc, this, slot.get());

					// A thread name holds 15 characters; a longer worker name keeps its front
					::pthread_setname_np(slot->thread.native_handle(), worker_name.Substring(0, 15).CStr());

					slot->running = true;
				}
				catch (const std::system_error &e)
				{
					// Post() must not throw at a caller that only expects false back
					logte("Could not start the dedicated worker '%s': %s", worker_name.CStr(), e.what());

					if (created_now)
					{
						_dedicated_workers.erase(worker_name);
					}

					return false;
				}
			}

			slot->task_queue.push(std::move(task));

			// Notified under the lock: Stop() destroys the worker objects under this same
			// lock, so a notify after releasing it could hit a destroyed condition
			slot->condition.notify_one();
		}

		return true;
	}

	size_t TaskPool::GetPendingCount() const
	{
		std::lock_guard<std::mutex> lock(_mutex);

		size_t count = 0;
		for (const auto &queue : _queues)
		{
			count += queue.ready.size() + queue.delayed.size();
		}

		return count;
	}

	size_t TaskPool::GetThreadCount() const
	{
		std::lock_guard<std::mutex> lock(_mutex);

		return _workers.size();
	}

	void TaskPool::Stop()
	{
		// A second caller waits here until the workers are joined
		std::lock_guard<std::mutex> stop_lock(_stop_mutex);

		std::vector<std::thread> workers;

		{
			std::lock_guard<std::mutex> lock(_mutex);

			if (_stopped)
			{
				return;
			}

			// Joining its own thread would throw
			auto current_thread_id = std::this_thread::get_id();
			for (const auto &worker : _workers)
			{
				if (worker.get_id() == current_thread_id)
				{
					logte("A task tried to stop the pool it runs on, which it cannot do");
					return;
				}
			}
			for (const auto &[name, worker] : _dedicated_workers)
			{
				if (worker->thread.get_id() == current_thread_id)
				{
					logte("A task tried to stop the pool it runs on, which it cannot do");
					return;
				}
			}

			_stopped = true;

			// Dropped, not run; what they would touch may already be shutting down
			for (auto &queue : _queues)
			{
				std::queue<Task> empty_queue;
				queue.ready.swap(empty_queue);
				queue.delayed.clear();
			}

			for (auto &[name, worker] : _dedicated_workers)
			{
				std::queue<Task> empty_worker_queue;
				worker->task_queue.swap(empty_worker_queue);
			}

			workers.swap(_workers);
		}

		_condition.notify_all();
		for (auto &[name, worker] : _dedicated_workers)
		{
			worker->condition.notify_all();
		}

		for (auto &worker : workers)
		{
			if (worker.joinable())
			{
				worker.join();
			}
		}

		// Joined before the map releases what the workers use
		for (auto &[name, worker] : _dedicated_workers)
		{
			if (worker->thread.joinable())
			{
				worker->thread.join();
			}
		}

		{
			std::lock_guard<std::mutex> lock(_mutex);
			_dedicated_workers.clear();
		}
	}

	void TaskPool::DedicatedWorkerThreadProc(DedicatedWorker *worker)
	{
		while (true)
		{
			Task task;

			{
				std::unique_lock<std::mutex> lock(_mutex);

				if (worker->keep_alive)
				{
					worker->condition.wait(lock, [this, worker]() {
						return _stopped || (worker->task_queue.empty() == false);
					});
				}

				if (_stopped)
				{
					break;
				}

				if (worker->task_queue.empty())
				{
					// Only reached without keep_alive: the queue ran dry, the worker leaves
					worker->running = false;
					break;
				}

				task = std::move(worker->task_queue.front());
				worker->task_queue.pop();
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

	std::optional<std::chrono::steady_clock::time_point> TaskPool::MoveDueTasks()
	{
		auto now = std::chrono::steady_clock::now();
		std::optional<std::chrono::steady_clock::time_point> next_due;

		for (auto &queue : _queues)
		{
			// Its time has come, so it lines up behind the tasks of its priority already waiting
			while ((queue.delayed.empty() == false) && (queue.delayed.begin()->first <= now))
			{
				queue.ready.push(std::move(queue.delayed.begin()->second));
				queue.delayed.erase(queue.delayed.begin());
			}

			if (queue.delayed.empty() == false)
			{
				auto due = queue.delayed.begin()->first;
				if ((next_due.has_value() == false) || (due < next_due.value()))
				{
					next_due = due;
				}
			}
		}

		return next_due;
	}

	bool TaskPool::HasWaitingTasks() const
	{
		for (const auto &queue : _queues)
		{
			if ((queue.ready.empty() == false) || (queue.delayed.empty() == false))
			{
				return true;
			}
		}

		return false;
	}

	void TaskPool::WorkerThreadProc()
	{
		while (true)
		{
			Task task;
			bool wake_another = false;

			{
				std::unique_lock<std::mutex> lock(_mutex);

				std::queue<Task> *ready_queue = nullptr;

				while (_stopped == false)
				{
					auto next_due = MoveDueTasks();

					// In priority order
					for (auto &queue : _queues)
					{
						if (queue.ready.empty() == false)
						{
							ready_queue = &queue.ready;
							break;
						}
					}

					if (ready_queue != nullptr)
					{
						break;
					}

					if (next_due.has_value())
					{
						_condition.wait_until(lock, next_due.value());
					}
					else
					{
						_condition.wait(lock);
					}
				}

				if (_stopped)
				{
					break;
				}

				task = std::move(ready_queue->front());
				ready_queue->pop();

				// Another idle worker takes what is left, and keeps watch for the next due task while this one is busy
				wake_another = HasWaitingTasks();
			}

			if (wake_another)
			{
				_condition.notify_one();
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
