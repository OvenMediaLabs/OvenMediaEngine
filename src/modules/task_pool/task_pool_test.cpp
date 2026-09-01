//==============================================================================
//
//  OvenMediaEngine - Unit Tests
//
//  src/modules/task_pool/task_pool_test.cpp
//  Covers: TaskPool (posting, results, worker isolation, queue limit, concurrent
//          posting, stop, dedicated workers)
//
//==============================================================================
#include <gtest/gtest.h>

#include <modules/task_pool/task_pool.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <optional>
#include <set>
#include <vector>

namespace
{
	constexpr auto kWaitTimeout = std::chrono::seconds(5);

	// Blocks a worker until opened; the wait is bounded so a failed test cannot hang the binary
	class TaskGate
	{
	public:
		void Wait()
		{
			_future.wait_for(kWaitTimeout);
		}

		void Open()
		{
			_promise.set_value();
		}

	private:
		std::promise<void> _promise;
		std::shared_future<void> _future{_promise.get_future()};
	};

	// The counter is on the heap because a task may outlive a failed wait
	struct StartCounter
	{
		std::mutex mutex;
		std::condition_variable condition;
		size_t count = 0;
	};

	[[nodiscard]] bool OccupyWorkers(ov::TaskPool &pool, TaskGate &gate, size_t worker_count)
	{
		auto counter = std::make_shared<StartCounter>();

		for (size_t index = 0; index < worker_count; index++)
		{
			if (pool.Post([counter, &gate]() {
					{
						std::lock_guard<std::mutex> lock(counter->mutex);
						counter->count++;
					}
					counter->condition.notify_all();

					gate.Wait();
				}) == false)
			{
				return false;
			}
		}

		std::unique_lock<std::mutex> lock(counter->mutex);
		return counter->condition.wait_for(lock, kWaitTimeout, [&counter, worker_count]() {
			return counter->count == worker_count;
		});
	}

	ov::TaskPool::Config MakeConfig(size_t thread_count, size_t max_tasks)
	{
		ov::TaskPool::Config config;

		config.thread_count = thread_count;
		config.max_tasks	= max_tasks;

		return config;
	}

	// An exited worker disappears here, unlike one that is merely idle. Nullopt on a
	// platform without a readable /proc, so the caller can skip instead of aborting
	std::optional<size_t> CountProcessThreads()
	{
		std::error_code error;
		std::filesystem::directory_iterator entries("/proc/self/task", error);
		if (error)
		{
			return std::nullopt;
		}

		size_t count = 0;

		for ([[maybe_unused]] const auto &entry : entries)
		{
			count++;
		}

		return count;
	}
}  // namespace

TEST(TaskPool, RunsPostedTask)
{
	ov::TaskPool pool;
	std::promise<int> promise;
	auto future = promise.get_future();

	ASSERT_TRUE(pool.Post([&promise]() {
		promise.set_value(42);
	}));

	ASSERT_EQ(future.wait_for(kWaitTimeout), std::future_status::ready);
	EXPECT_EQ(future.get(), 42);
}

TEST(TaskPool, SubmitHandsBackTheResult)
{
	ov::TaskPool pool;

	auto future = pool.Submit([]() {
		return ov::String("done");
	});

	ASSERT_EQ(future.wait_for(kWaitTimeout), std::future_status::ready);
	EXPECT_STREQ(future.get().CStr(), "done");
}

TEST(TaskPool, SubmitReportsAnExceptionThrownByTheTask)
{
	ov::TaskPool pool;

	auto future = pool.Submit([]() -> int {
		throw std::runtime_error("failed");
	});

	ASSERT_EQ(future.wait_for(kWaitTimeout), std::future_status::ready);
	EXPECT_THROW(future.get(), std::runtime_error);
}

// A non-temporary callable is deduced as a reference, which the return type has to survive
TEST(TaskPool, SubmitTakesACallableThatIsNotATemporary)
{
	ov::TaskPool pool;

	auto work = []() {
		return 7;
	};

	auto future = pool.Submit(work);

	ASSERT_EQ(future.wait_for(kWaitTimeout), std::future_status::ready);
	EXPECT_EQ(future.get(), 7);
}

TEST(TaskPool, StartsTheWorkersWithTheFirstTask)
{
	ov::TaskPool pool;

	pool.Configure(MakeConfig(4, 128));

	EXPECT_EQ(pool.GetThreadCount(), 0u);

	std::promise<void> promise;
	auto future = promise.get_future();

	ASSERT_TRUE(pool.Post([&promise]() {
		promise.set_value();
	}));

	ASSERT_EQ(future.wait_for(kWaitTimeout), std::future_status::ready);
	EXPECT_EQ(pool.GetThreadCount(), 4u);
}

TEST(TaskPool, RunsTasksOnEveryWorkerAtOnce)
{
	// Declared before the pool so that the workers are joined while the gate still exists
	TaskGate gate;
	ov::TaskPool pool;

	pool.Configure(MakeConfig(4, 128));

	EXPECT_TRUE(OccupyWorkers(pool, gate, 4));

	gate.Open();
}

TEST(TaskPool, WorkerSurvivesATaskThatThrows)
{
	ov::TaskPool pool;
	std::promise<void> promise;
	auto future = promise.get_future();

	ASSERT_TRUE(pool.Post([]() {
		throw std::runtime_error("failed");
	}));

	ASSERT_TRUE(pool.Post([&promise]() {
		promise.set_value();
	}));

	EXPECT_EQ(future.wait_for(kWaitTimeout), std::future_status::ready);
}

TEST(TaskPool, RejectsATaskOnceTooManyAreWaiting)
{
	TaskGate gate;
	ov::TaskPool pool;

	pool.Configure(MakeConfig(1, 2));

	// Nothing leaves the queue while the only worker is blocked
	ASSERT_TRUE(OccupyWorkers(pool, gate, 1));

	ASSERT_TRUE(pool.Post([]() {}));
	ASSERT_TRUE(pool.Post([]() {}));

	EXPECT_FALSE(pool.Post([]() {}));
	EXPECT_EQ(pool.GetPendingCount(), 2u);

	gate.Open();
}

TEST(TaskPool, RunsEveryTaskPostedFromManyThreadsAtOnce)
{
	constexpr size_t kPosterCount	 = 8;
	constexpr size_t kTasksPerPoster = 100;

	// Declared before the pool so that the workers are joined while the counters still exist
	std::atomic<size_t> done_count{0};
	std::atomic<size_t> rejected_count{0};

	ov::TaskPool pool;

	// Room to spare for every task, so that a rejection can only mean the pool lost one
	pool.Configure(MakeConfig(4, kPosterCount * kTasksPerPoster * 2));

	std::vector<std::thread> posters;

	for (size_t index = 0; index < kPosterCount; index++)
	{
		posters.emplace_back([&pool, &done_count, &rejected_count]() {
			for (size_t count = 0; count < kTasksPerPoster; count++)
			{
				if (pool.Post([&done_count]() {
						done_count++;
					}) == false)
				{
					rejected_count++;
				}
			}
		});
	}

	for (auto &poster : posters)
	{
		poster.join();
	}

	auto deadline = std::chrono::steady_clock::now() + kWaitTimeout;
	while ((done_count.load() < (kPosterCount * kTasksPerPoster)) && (std::chrono::steady_clock::now() < deadline))
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}

	EXPECT_EQ(rejected_count.load(), 0u);
	EXPECT_EQ(done_count.load(), kPosterCount * kTasksPerPoster);
	EXPECT_EQ(pool.GetPendingCount(), 0u);
}

TEST(TaskPool, StopWaitsForARunningTask)
{
	TaskGate gate;
	ov::TaskPool pool;

	std::atomic<bool> task_finished{false};
	std::promise<void> started;
	auto started_future = started.get_future();

	ASSERT_TRUE(pool.Post([&started, &gate, &task_finished]() {
		started.set_value();
		gate.Wait();
		task_finished = true;
	}));
	ASSERT_EQ(started_future.wait_for(kWaitTimeout), std::future_status::ready);

	// Held long enough that a Stop() which did not wait would be back before the task is
	std::thread opener([&gate]() {
		std::this_thread::sleep_for(std::chrono::milliseconds(200));
		gate.Open();
	});

	pool.Stop();

	EXPECT_TRUE(task_finished.load());

	opener.join();
}

TEST(TaskPool, BreaksThePromiseOfATaskDroppedByStop)
{
	TaskGate gate;
	ov::TaskPool pool;

	pool.Configure(MakeConfig(1, 128));

	ASSERT_TRUE(OccupyWorkers(pool, gate, 1));

	// Waits behind the blocked worker, so Stop() drops it before it can run
	auto future = pool.Submit([]() {
		return 1;
	});

	std::thread opener([&gate]() {
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
		gate.Open();
	});

	pool.Stop();
	opener.join();

	ASSERT_EQ(future.wait_for(kWaitTimeout), std::future_status::ready);
	EXPECT_THROW(future.get(), std::future_error);
}

TEST(TaskPool, TakesNoTaskAfterItIsStopped)
{
	ov::TaskPool pool;

	ASSERT_TRUE(pool.Post([]() {}));

	pool.Stop();

	EXPECT_FALSE(pool.Post([]() {}));

	auto future = pool.Submit([]() {
		return 1;
	});

	ASSERT_EQ(future.wait_for(kWaitTimeout), std::future_status::ready);
	EXPECT_THROW(future.get(), std::future_error);
}

// Stop() from a task would join the very thread that asks
TEST(TaskPool, RefusesToStopFromItsOwnTask)
{
	ov::TaskPool pool;

	std::promise<void> promise;
	auto future = promise.get_future();

	ASSERT_TRUE(pool.Post([&pool, &promise]() {
		pool.Stop();
		promise.set_value();
	}));

	ASSERT_EQ(future.wait_for(kWaitTimeout), std::future_status::ready);

	// The pool is left as it was, so it still takes work
	std::promise<void> next_promise;
	auto next_future = next_promise.get_future();

	ASSERT_TRUE(pool.Post([&next_promise]() {
		next_promise.set_value();
	}));
	EXPECT_EQ(next_future.wait_for(kWaitTimeout), std::future_status::ready);
}

TEST(TaskPool, StopIsSafeToCallTwice)
{
	ov::TaskPool pool;

	ASSERT_TRUE(pool.Post([]() {}));

	pool.Stop();
	pool.Stop();
}

//------------------------------------------------------------------------------
// Dedicated workers
//------------------------------------------------------------------------------

TEST(TaskPool, ADedicatedWorkerRunsTasksInOrderOnOneThread)
{
	ov::TaskPool pool;
	TaskGate gate;

	std::mutex mutex;
	std::vector<int> order;
	std::set<std::thread::id> thread_ids;
	std::promise<void> done_promise;
	auto done_future = done_promise.get_future();

	// The first task holds the worker so the rest pile up behind it
	ASSERT_TRUE(pool.PostDedicated("worker-a", [&]() {
		{
			std::lock_guard<std::mutex> lock(mutex);
			thread_ids.insert(std::this_thread::get_id());
		}

		gate.Wait();
	}));

	for (int index = 0; index < 5; index++)
	{
		ASSERT_TRUE(pool.PostDedicated("worker-a", [&, index]() {
			std::lock_guard<std::mutex> lock(mutex);
			order.push_back(index);
			thread_ids.insert(std::this_thread::get_id());

			if (index == 4)
			{
				done_promise.set_value();
			}
		}));
	}

	gate.Open();

	ASSERT_EQ(done_future.wait_for(kWaitTimeout), std::future_status::ready);

	std::lock_guard<std::mutex> lock(mutex);
	EXPECT_EQ(order, (std::vector<int>{0, 1, 2, 3, 4}));
	EXPECT_EQ(thread_ids.size(), 1u);
}

TEST(TaskPool, ABlockedDedicatedWorkerLeavesTheSharedWorkersFree)
{
	ov::TaskPool pool;
	pool.Configure(MakeConfig(1, 8));

	TaskGate gate;

	// The dedicated worker is held on the gate for the whole test
	ASSERT_TRUE(pool.PostDedicated("worker-b", [&gate]() {
		gate.Wait();
	}));

	// The shared worker still takes and runs work
	std::promise<void> shared_promise;
	auto shared_future = shared_promise.get_future();

	ASSERT_TRUE(pool.Post([&shared_promise]() {
		shared_promise.set_value();
	}));
	EXPECT_EQ(shared_future.wait_for(kWaitTimeout), std::future_status::ready);

	gate.Open();
}

TEST(TaskPool, TheSameNameSharesOneDedicatedWorker)
{
	ov::TaskPool pool;
	TaskGate gate;

	std::mutex mutex;
	std::set<std::thread::id> thread_ids;
	std::promise<void> done_promise;
	auto done_future = done_promise.get_future();

	// The first task holds the worker so both land on the same one
	ASSERT_TRUE(pool.PostDedicated("worker-c", [&]() {
		{
			std::lock_guard<std::mutex> lock(mutex);
			thread_ids.insert(std::this_thread::get_id());
		}

		gate.Wait();
	}));
	ASSERT_TRUE(pool.PostDedicated("worker-c", [&]() {
		{
			std::lock_guard<std::mutex> lock(mutex);
			thread_ids.insert(std::this_thread::get_id());
		}
		done_promise.set_value();
	}));

	gate.Open();

	ASSERT_EQ(done_future.wait_for(kWaitTimeout), std::future_status::ready);

	std::lock_guard<std::mutex> lock(mutex);
	EXPECT_EQ(thread_ids.size(), 1u);
}

TEST(TaskPool, ADedicatedWorkerRejectsATaskOnceTooManyAreWaiting)
{
	ov::TaskPool pool;
	pool.Configure(MakeConfig(1, 2));

	TaskGate gate;

	// The queue is filled only after the first task is known to be running, off the queue
	std::promise<void> started_promise;
	auto started_future = started_promise.get_future();

	ASSERT_TRUE(pool.PostDedicated("worker-d", [&gate, &started_promise]() {
		started_promise.set_value();
		gate.Wait();
	}));
	ASSERT_EQ(started_future.wait_for(kWaitTimeout), std::future_status::ready);

	ASSERT_TRUE(pool.PostDedicated("worker-d", []() {}));
	ASSERT_TRUE(pool.PostDedicated("worker-d", []() {}));

	EXPECT_FALSE(pool.PostDedicated("worker-d", []() {}));

	gate.Open();
}

TEST(TaskPool, TakesNoDedicatedTaskAfterItIsStopped)
{
	ov::TaskPool pool;

	ASSERT_TRUE(pool.PostDedicated("worker-e", []() {}));

	pool.Stop();

	EXPECT_FALSE(pool.PostDedicated("worker-e", []() {}));
}

TEST(TaskPool, StopWaitsForARunningDedicatedTask)
{
	auto pool = std::make_unique<ov::TaskPool>();

	std::atomic<bool> finished{false};
	std::promise<void> started_promise;
	auto started_future = started_promise.get_future();

	ASSERT_TRUE(pool->PostDedicated("worker-f", [&]() {
		started_promise.set_value();
		std::this_thread::sleep_for(std::chrono::milliseconds(300));
		finished = true;
	}));

	ASSERT_EQ(started_future.wait_for(kWaitTimeout), std::future_status::ready);

	pool->Stop();
	EXPECT_TRUE(finished.load());
}

TEST(TaskPool, ADedicatedWorkerLeavesOnceItsQueueRunsDry)
{
	ov::TaskPool pool;

	const auto counted = CountProcessThreads();
	if (counted.has_value() == false)
	{
		GTEST_SKIP() << "No readable /proc/self/task on this platform";
	}
	const size_t baseline = *counted;

	std::promise<void> first_done;
	auto first_future = first_done.get_future();

	ASSERT_TRUE(pool.PostDedicated("worker-g", [&first_done]() {
		first_done.set_value();
	}));
	ASSERT_EQ(first_future.wait_for(kWaitTimeout), std::future_status::ready);

	// Only the worker's exit brings the thread count back down
	auto deadline = std::chrono::steady_clock::now() + kWaitTimeout;
	while ((CountProcessThreads().value_or(baseline) > baseline) && (std::chrono::steady_clock::now() < deadline))
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}
	EXPECT_EQ(CountProcessThreads().value_or(baseline), baseline);

	// A post after the worker left starts a fresh one and still runs
	std::promise<void> second_done;
	auto second_future = second_done.get_future();

	ASSERT_TRUE(pool.PostDedicated("worker-g", [&second_done]() {
		second_done.set_value();
	}));
	EXPECT_EQ(second_future.wait_for(kWaitTimeout), std::future_status::ready);
}

TEST(TaskPool, AKeepAliveDedicatedWorkerStaysBetweenTasks)
{
	ov::TaskPool pool;

	std::promise<std::thread::id> first_id;
	auto first_future = first_id.get_future();

	ASSERT_TRUE(pool.PostDedicated("worker-h", [&first_id]() {
		first_id.set_value(std::this_thread::get_id());
	}, true));
	ASSERT_EQ(first_future.wait_for(kWaitTimeout), std::future_status::ready);

	// Long enough that a worker which leaves when idle would be gone by now
	std::this_thread::sleep_for(std::chrono::milliseconds(100));

	std::promise<std::thread::id> second_id;
	auto second_future = second_id.get_future();

	ASSERT_TRUE(pool.PostDedicated("worker-h", [&second_id]() {
		second_id.set_value(std::this_thread::get_id());
	}, true));
	ASSERT_EQ(second_future.wait_for(kWaitTimeout), std::future_status::ready);

	EXPECT_EQ(first_future.get(), second_future.get());
}
