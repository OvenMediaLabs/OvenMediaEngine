//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2026 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <base/ovlibrary/ovlibrary.h>

#include <array>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <future>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace ov
{
	// Shared worker threads for short tasks that mostly wait (a name lookup, a remote
	// request). GetInstance() gives the pool the modules share; PostDedicated gives a
	// module a worker of its own.
	//
	// A task must not wait for another task of this pool and must not call Stop().
	// A task outlives the call that posted it, so capture a std::weak_ptr, not a raw this.
	class TaskPool : public Singleton<TaskPool>
	{
	public:
		using Task = std::function<void()>;

		// A worker takes the higher priorities first, so a lower task may wait behind them
		enum class Priority
		{
			High,
			Normal,
			Low
		};

		// A max_tasks that never rejects, for work that must not be dropped, such as closing a session
		static constexpr size_t kNoLimit = std::numeric_limits<size_t>::max();

		struct Config
		{
			// Sized for tasks that wait, not for the core count
			size_t thread_count = 4;
			// For a task posted without a max of its own; tasks over it are rejected, not queued without end
			size_t max_tasks = 128;
		};

		TaskPool();
		~TaskPool() override;

		// Applies <Modules><TaskPool> from the server configuration; call before any task is posted
		bool Initialize();

		// Applies a configuration directly; workers already running are not resized
		void Configure(const Config &config);

		// Runs the task on a shared worker. It is rejected when max_tasks tasks of its priority are
		// already waiting, the configured max_tasks unless given. Returns false when stopped or rejected
		bool Post(Task task, Priority priority = Priority::Normal, std::optional<size_t> max_tasks = std::nullopt);

		// Same as Post(), but the task runs once after_msec has passed
		bool PostDelayed(int after_msec, Task task, Priority priority = Priority::Normal, std::optional<size_t> max_tasks = std::nullopt);

		// Runs the task on a dedicated worker named worker_name, tasks in posted order.
		// Long-blocking work belongs here, off the shared workers. Callers using the same
		// name share one worker. The worker leaves once its queue runs dry and the next
		// post starts a new one; keep_alive keeps it parked instead. The flag of the post
		// that creates the worker stays.
		bool PostDedicated(const ov::String &worker_name, Task task, bool keep_alive = false);

		// Posts a task and returns its result as a future; a task that never runs breaks
		// the promise
		template <typename Func>
		auto Submit(Func &&func) -> std::future<std::invoke_result_t<Func>>
		{
			using ResultType = std::invoke_result_t<Func>;

			auto task = std::make_shared<std::packaged_task<ResultType()>>(std::forward<Func>(func));
			auto future = task->get_future();

			Post([task]() {
				(*task)();
			});

			return future;
		}

		// Tasks waiting to start
		size_t GetPendingCount() const;
		// Shared workers running now; they start with the first task
		size_t GetThreadCount() const;

		// Lets running tasks finish and drops the waiting ones; the pool takes no task afterwards
		void Stop();

	protected:
		struct DedicatedWorker
		{
			std::queue<Task> task_queue;
			// Only a keep-alive worker waits on this
			std::condition_variable condition;
			std::thread thread;
			bool keep_alive = false;
			// Cleared by the worker itself when it leaves for lack of work
			bool running = false;
		};

		struct PriorityQueue
		{
			std::queue<Task> ready;
			// Waiting for their time, earliest first; a due task joins ready
			std::multimap<std::chrono::steady_clock::time_point, Task> delayed;
		};

		void WorkerThreadProc();
		void DedicatedWorkerThreadProc(DedicatedWorker *worker);
		// Returns how many started. The caller holds _mutex
		size_t AddWorkers(size_t count);
		// Starts the shared workers with the first task. Returns false when none could start. The caller holds _mutex
		bool StartWorkersOnFirstTask();
		// Returns false when the pool takes no task of this priority now. The caller holds _mutex
		bool CanAccept(Priority priority, std::optional<size_t> max_tasks);
		PriorityQueue &GetQueue(Priority priority);
		// Moves the delayed tasks whose time has come to their ready queues and returns when the next one is due. The caller holds _mutex
		std::optional<std::chrono::steady_clock::time_point> MoveDueTasks();
		// Whether any task is ready or waiting for its time. The caller holds _mutex
		bool HasWaitingTasks() const;
		// Reports rejections at most once a second. The caller holds _mutex
		void ReportRejection();

		mutable std::mutex _mutex;
		std::condition_variable _condition;

		// A second Stop() waits here until the first has joined the workers
		std::mutex _stop_mutex;

		Config _config;
		// Indexed by Priority, taken in that order
		std::array<PriorityQueue, 3> _queues;
		std::vector<std::thread> _workers;

		// The worker objects must outlive their threads, so Stop() joins before clearing
		std::map<ov::String, std::unique_ptr<DedicatedWorker>> _dedicated_workers;

		size_t _rejected_count = 0;
		std::chrono::steady_clock::time_point _last_reject_log_time;

		bool _stopped = false;
	};
}  // namespace ov
