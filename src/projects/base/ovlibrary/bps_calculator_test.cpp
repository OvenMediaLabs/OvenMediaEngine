//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2026 OvenMediaLabs. All rights reserved.
//
//==============================================================================
#include <base/ovlibrary/bps_calculator.h>
#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

// Tests around the `BpsCalculator` destruction-order fix on this branch:
// `_timer` (`DelayQueue`) used to be destroyed AFTER the members
// its tick lambda touches (`_mutex`/`_bits`/`_acc_bits`);
// the explicit destructor now joins the timer thread first.
// Honest scope note: the tick interval is a hardcoded 1000ms,
// so the pre-fix UAF window (a tick in flight exactly at destruction)
// is practically impossible to hit deterministically from a unit test -
// `DestructionJoinsTimerThread` is a construct/destroy smoke, and
// `ConcurrentAddAndReadKeepsTotals` runs PAST the first tick so the timer lambda's
// exclusive section genuinely overlaps the readers/writers
// (TSan-meaningful for the `GetBps()` shared-lock fix).

TEST(BpsCalculator, DestructionJoinsTimerThread)
{
	// Each iteration starts a `DelayQueue` thread with a 1s tick and destroys
	// the calculator immediately; the destructor must join the thread
	// while every member it touches is still alive.
	// 200 iterations keeps the test fast while giving the old UAF window
	// plenty of chances to fire.
	for (int i = 0; i < 200; i++)
	{
		ov::BpsCalculator bps;
		bps.AddBits(1024);
	}
	SUCCEED();
}

TEST(BpsCalculator, ConcurrentAddAndReadKeepsTotals)
{
	ov::BpsCalculator bps;

	constexpr int kThreads = 4;
	constexpr int64_t kBitsPerAdd = 8;
	// Must outlive the 1000ms tick so the timer lambda's exclusive-lock section
	// actually runs concurrently with the readers/writers below
	constexpr auto kRunTime = std::chrono::milliseconds(1300);

	std::atomic<bool> stop{false};
	std::atomic<int64_t> added_total{0};

	std::thread reader([&] {
		while (stop.load() == false)
		{
			// Lock-grade fix under test: `GetBps()` takes the shared lock
			// that the timer lambda's exclusive section pairs with
			(void)bps.GetBps();
			(void)bps.GetTotalBits();
		}
	});

	std::vector<std::thread> writers;
	for (int t = 0; t < kThreads; t++)
	{
		writers.emplace_back([&] {
			while (stop.load() == false)
			{
				bps.AddBits(kBitsPerAdd);
				added_total += kBitsPerAdd;
			}
		});
	}

	std::this_thread::sleep_for(kRunTime);
	stop = true;

	for (auto &writer : writers)
	{
		writer.join();
	}
	reader.join();

	EXPECT_EQ(bps.GetTotalBits(), added_total.load());
}
