//==============================================================================
//
//  OvenMediaEngine - Unit Tests
//
//  src/modules/task_pool/task_pool_test.cpp
//  Covers: TaskPool (posting, results, worker isolation, queue limit, concurrent
//          posting, stop)
//
//==============================================================================
#include <gtest/gtest.h>

#include <modules/task_pool/task_pool.h>

#include <atomic>
#include <chrono>

namespace
{
	constexpr auto kWaitTimeout = std::chrono::seconds(5);

	// Blocks the workers so that the queue can be observed while they are busy. The wait is
	// bounded because a failed assertion returns before the gate is opened, and a worker left
	// waiting for good would hang the whole binary once the pool joins it.
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

	// Leaves every worker blocked on the gate, so that what follows sees a pool that takes
	// tasks but runs none of them. The counter is shared with the tasks rather than held on
	// the stack, because a task still counts up after a failed wait gives up on it.
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

// A callable that is not a temporary is deduced as a reference, which the return type has to
// survive
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

// Every worker has to be able to run at the same time, otherwise a task that waits would
// hold up the tasks of every other module
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

// Whatever a running task is holding must still be valid until it returns, so Stop() cannot
// come back while one is in the middle of its work
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

// Stopping from a task would have the pool join the very thread that asks, which takes the
// process down rather than just failing
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
