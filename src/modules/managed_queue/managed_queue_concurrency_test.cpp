//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2026 OvenMediaLabs. All rights reserved.
//
//==============================================================================
#include <modules/managed_queue/managed_queue.h>
#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <future>
#include <thread>

// Tests for the `ManagedQueue` thread-safety changes on this branch:
// - the base metric members became atomics
//   ("writes only under the derived queue's `_mutex`" convention)
//   so lock-free monitoring reads are well-defined while traffic is flowing
// - the enqueue stop/full log paths null-guard `_urn`
//   (a default-constructed queue has no URN; the old code dereferenced it unconditionally)

TEST(ManagedQueueConcurrency, StopDropWithNullUrnDoesNotCrash)
{
	// Default-constructed queue: URN is `nullptr` (`SetUrn()` was never called).
	// Fill to the threshold, block a producer in the exceed-wait predicate,
	// then `Stop()` - the drop path logs with the null-guarded URN.
	ov::ManagedQueue<int> queue;
	queue.SetThreshold(1);
	queue.SetExceedWaitEnable(true);

	queue.Enqueue(1);  // size == threshold -> next enqueue waits

	std::promise<void> producer_returned;
	std::thread producer([&] {
		// Bounded timeout: even if `Stop()` fails to wake this thread
		// (the regression under guard), the producer self-unblocks and the test
		// can FAIL instead of `std::terminate`-ing the whole binary
		queue.Enqueue(2, false, 10000);
		producer_returned.set_value();
	});

	std::this_thread::sleep_for(std::chrono::milliseconds(50));
	queue.Stop();

	auto future = producer_returned.get_future();
	const bool woke_in_time = (future.wait_for(std::chrono::seconds(5)) == std::future_status::ready);
	producer.join();

	EXPECT_TRUE(woke_in_time) << "Stop() did not release the blocked producer";
	EXPECT_TRUE(queue.IsStopped());
}

TEST(ManagedQueueConcurrency, FullDropTimeoutWithNullUrnDoesNotCrash)
{
	// The actually-fixed crash path: the timeout-expiry "queue is full" drop
	// used to dereference `_urn` unconditionally (`nullptr` for a default-constructed queue).
	// Fill to the threshold and let an exceed-wait enqueue time out.
	ov::ManagedQueue<int> queue;
	queue.SetThreshold(1);
	queue.SetExceedWaitEnable(true);

	queue.Enqueue(1);				// size == threshold
	queue.Enqueue(2, false, 50);	// waits 50ms, expires -> full-drop log with null URN

	EXPECT_EQ(queue.Size(), 1u);	// the timed-out item was dropped, not enqueued
}

TEST(ManagedQueueConcurrency, LockFreeMetricReadsDuringTraffic)
{
	ov::ManagedQueue<int> queue;

	std::atomic<bool> stop{false};
	std::atomic<bool> metrics_sane{true};
	std::atomic<uint64_t> metric_reads{0};

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
			(void)queue.Dequeue(1);
		}
	});

	std::thread monitor([&] {
		while (stop.load() == false)
		{
			// These are the lock-free reads the atomic conversion legalized;
			// before the conversion they were plain reads racing `_mutex`-held writers
			// (UB - only TSan can catch the pre-fix defect on x86_64,
			// so the bound below is a sanity pin, not the primary oracle).
			// `GetInfoString()` additionally formats them via `.load()`.
			auto size = queue.GetSize();
			auto peak = queue.GetPeak();
			(void)queue.GetInfoString();

			// `_peak` is monotonic while traffic flows and is raised
			// in the same `_mutex` section that grows `_size`;
			// with a single producer a reader loading size-then-peak
			// can be ahead by at most one in-flight item
			if (peak > 0 && size > peak + 1)
			{
				metrics_sane = false;
			}
			metric_reads++;
		}
	});

	std::this_thread::sleep_for(std::chrono::milliseconds(200));
	stop = true;

	producer.join();
	queue.Stop();
	consumer.join();
	monitor.join();

	EXPECT_TRUE(metrics_sane.load());
	EXPECT_GT(metric_reads.load(), 0u);
}
