//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2026 OvenMediaLabs. All rights reserved.
//
//==============================================================================
#include <base/ovlibrary/queue.h>
#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <future>
#include <thread>

// Tests around the `ov::Queue` thread-safety changes on this branch.
// Honest scope note: `StopWakesBlockedDequeue` guards the CV wakeup contract
// (a store-outside-mutex regression would make it flaky/failing);
// the other two are behavior pins/TSan smoke - the `_threshold` atomic conversion
// and the dead-lock removal in `SetThreshold()` have no deterministic failure mode
// in a  plain build (the pre-fix defect was a data race only TSan can observe).

TEST(OvQueue, StopWakesBlockedDequeue)
{
	ov::Queue<int> queue("OvQueueTest", 0);

	std::promise<bool> dequeue_returned;
	std::thread consumer([&] {
		// Bounded timeout: if `Stop()` fails to wake this thread (the regression
		// under guard), the consumer self-unblocks so the test can FAIL instead
		// of `std::terminate`-ing the whole binary on a joinable-thread dtor
		auto item = queue.Dequeue(10000);
		dequeue_returned.set_value(item.has_value() == false);
	});

	std::this_thread::sleep_for(std::chrono::milliseconds(50));
	queue.Stop();

	auto future				= dequeue_returned.get_future();
	const bool woke_in_time = (future.wait_for(std::chrono::seconds(5)) == std::future_status::ready);
	consumer.join();

	EXPECT_TRUE(woke_in_time) << "Stop() did not wake the blocked Dequeue() (lost wakeup)";
	EXPECT_TRUE(queue.IsStopped());
}

TEST(OvQueue, ThresholdExceedanceOnlyLogsAndKeepsItems)
{
	ov::Queue<int> queue("OvQueueTest", 3 /* threshold */);

	for (int i = 0; i < 5; i++)
	{
		queue.Enqueue(i);
	}

	// `CheckThreshold()` must not clear the queue - it only tracks the peak and logs
	EXPECT_EQ(queue.Size(), 5u);

	for (int i = 0; i < 5; i++)
	{
		auto item = queue.Dequeue(0);
		ASSERT_TRUE(item.has_value());
		EXPECT_EQ(*item, i);
	}
}

TEST(OvQueue, ConcurrentSetThresholdWithTraffic)
{
	// `SetThreshold()` is now a bare atomic store racing `CheckThreshold()`'s
	// acquire load inside `Enqueue()`; this must be free of crashes/deadlocks
	// and must not disturb the item flow.
	ov::Queue<int> queue("OvQueueTest", 1);

	std::atomic<bool> stop{false};
	std::atomic<uint64_t> dequeued{0};

	std::thread threshold_toggler([&] {
		size_t threshold = 0;
		while (stop.load() == false)
		{
			queue.SetThreshold(threshold);
			threshold = (threshold + 7) % 32;
		}
	});

	std::thread producer([&] {
		int value = 0;
		while (stop.load() == false)
		{
			queue.Enqueue(value++);
		}
	});

	std::thread consumer([&] {
		while (stop.load() == false)
		{
			if (queue.Dequeue(1).has_value())
			{
				dequeued++;
			}
		}
	});

	std::this_thread::sleep_for(std::chrono::milliseconds(200));
	stop = true;

	threshold_toggler.join();
	producer.join();
	queue.Stop();
	consumer.join();

	EXPECT_GT(dequeued.load(), 0u);
}
