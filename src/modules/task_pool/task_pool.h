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

#include <chrono>
#include <condition_variable>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace ov
{
	// Runs short tasks on shared worker threads so that modules do not have to own a thread
	// each. It is meant for work that mostly waits, such as a name lookup or a request to a
	// remote service, rather than for work that keeps a CPU busy.
	//
	// The workers start with the first task and stay until the pool stops.
	//
	// GetInstance() gives the pool the modules share. A caller that needs workers of its own,
	// for work it does not want to leave behind other modules, can construct one instead.
	//
	//     // Run it and move on
	//     ov::TaskPool::GetInstance()->Post([]() { DoWork(); });
	//
	//     // Run it and pick the result up later
	//     auto future = ov::TaskPool::GetInstance()->Submit([]() { return DoWork(); });
	//     auto result = future.get();
	//
	// A task must never wait for another task of this pool to finish, and must never call
	// Stop(): every worker could end up waiting and none would be left to run what they wait
	// for, and Stop() from a task would join the thread it runs on.
	//
	// A posted task cannot be cancelled and outlives the call that posted it, so it must not
	// capture a raw `this` of anything that can go away while it waits to run. Capture a
	// std::weak_ptr and check it instead.
	//
	//     std::weak_ptr<Stream> weak_stream = stream;
	//     ov::TaskPool::GetInstance()->Post([weak_stream]() {
	//         auto stream = weak_stream.lock();
	//         if (stream != nullptr) { ... }
	//     });
	class TaskPool : public Singleton<TaskPool>
	{
	public:
		using Task = std::function<void()>;

		struct Config
		{
			// Workers the pool runs on. Sized for tasks that wait rather than compute, so it
			// does not follow the core count.
			size_t thread_count = 4;
			// Tasks that may wait to start. Reaching this means the producers outrun the
			// workers, and further tasks are rejected rather than queued without end.
			size_t max_tasks = 128;
		};

		TaskPool();
		~TaskPool() override;

		// Applies the <TaskPool> settings of the server configuration. The workers already
		// running are not resized to a new thread count, so this belongs in the startup path
		// before any module posts a task.
		bool Initialize();

		// Applies a configuration directly, for callers that do not take it from the server
		// configuration. The workers already running are left as they are.
		void Configure(const Config &config);

		// Hands the task to a worker and returns without waiting for it. Returns false when
		// the pool is stopped, when the queue is full, or when no worker could be started.
		bool Post(Task task);

		// Posts a task and hands back its result through a future. When the task cannot be
		// posted, or the pool stops before it runs, the future reports a broken promise.
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

		// Tasks waiting to start, not counting the ones already running
		size_t GetPendingCount() const;
		// Workers running right now, which is zero until the first task arrives
		size_t GetThreadCount() const;

		// Lets the running tasks finish and drops the ones still waiting. The pool does not
		// take tasks again afterwards, so this belongs in the shutdown path.
		void Stop();

	protected:
		void WorkerThreadProc();
		// Adds up to `count` workers and returns how many started. The caller holds _mutex.
		size_t AddWorkers(size_t count);
		// Counts a rejected task and reports the count at most once a second. The caller
		// holds _mutex.
		void ReportRejection();

		mutable std::mutex _mutex;
		std::condition_variable _condition;

		// Serializes Stop() itself, so that a caller which finds the pool already stopped
		// still waits until the workers are joined
		std::mutex _stop_mutex;

		Config _config;
		std::queue<Task> _task_queue;
		std::vector<std::thread> _workers;

		// A full queue rejects every task that follows, so the rejections are counted and
		// reported at intervals rather than one log line each
		size_t _rejected_count = 0;
		std::chrono::steady_clock::time_point _last_reject_log_time;

		bool _stopped = false;
	};
}  // namespace ov
