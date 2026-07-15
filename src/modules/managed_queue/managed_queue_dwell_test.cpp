//==============================================================================
//
//  OvenMediaEngine
//
//  Copyright (c) 2026 OvenMediaLabs. All rights reserved.
//
//==============================================================================
#include <base/info/managed_queue.h>
#include <gtest/gtest.h>

#include <cmath>

// The dwell-time statistics live in info::ManagedQueue and are pure (no
// MonitorInstance), so they are exercised directly here. RecordDwellUs feeds both
// the cumulative (whole-lifetime) set and the current-interval set; RollDwellLatest
// publishes the interval as the "latest" values and resets it for the next window.

TEST(ManagedQueueDwell, LatestPublishesIntervalThenResets)
{
	info::ManagedQueue queue;

	// Samples land in log2 buckets whose reported value is the bucket lower bound;
	// 8, 16 and 1024 are exact powers of two, so they report unchanged.
	queue.RecordDwellUs(8);
	queue.RecordDwellUs(16);
	queue.RecordDwellUs(1024);

	queue.RollDwellLatest();

	// Latest reflects this interval. avg is exact (sum/count); percentiles are the
	// nearest-rank bucket lower bound over the sorted samples {8, 16, 1024}.
	EXPECT_EQ(queue.GetDwellLatestMinUs(), 8);
	EXPECT_EQ(queue.GetDwellLatestMaxUs(), 1024);
	EXPECT_EQ(queue.GetDwellLatestAvgUs(), (8 + 16 + 1024) / 3);
	EXPECT_EQ(queue.GetDwellLatestP50Us(), 16);
	EXPECT_EQ(queue.GetDwellLatestP90Us(), 1024);
	EXPECT_EQ(queue.GetDwellLatestP99Us(), 1024);

	// The cumulative set reflects the same samples.
	EXPECT_EQ(queue.GetDwellMinUs(), 8);
	EXPECT_EQ(queue.GetDwellMaxUs(), 1024);

	// A second roll with no new samples clears the latest values but leaves the
	// cumulative distribution intact.
	queue.RollDwellLatest();

	EXPECT_EQ(queue.GetDwellLatestMinUs(), 0);
	EXPECT_EQ(queue.GetDwellLatestAvgUs(), 0);
	EXPECT_EQ(queue.GetDwellLatestP50Us(), 0);
	EXPECT_EQ(queue.GetDwellLatestP90Us(), 0);
	EXPECT_EQ(queue.GetDwellLatestP99Us(), 0);
	EXPECT_EQ(queue.GetDwellLatestMaxUs(), 0);

	EXPECT_EQ(queue.GetDwellMinUs(), 8);
	EXPECT_EQ(queue.GetDwellMaxUs(), 1024);
}

// GetDwellPercentileUs clamps its input to [0, 1]; out-of-range and NaN must not
// wrap the unsigned rank or read out of bounds.
TEST(ManagedQueueDwell, PercentileInputIsClamped)
{
	info::ManagedQueue queue;

	queue.RecordDwellUs(8);
	queue.RecordDwellUs(1024);

	// Below 0 (and NaN) behave as 0.0 -> smallest sample; above 1 behaves as max.
	EXPECT_EQ(queue.GetDwellPercentileUs(-1.0), queue.GetDwellPercentileUs(0.0));
	EXPECT_EQ(queue.GetDwellPercentileUs(2.0), queue.GetDwellPercentileUs(1.0));
	EXPECT_EQ(queue.GetDwellPercentileUs(std::nan("")), queue.GetDwellPercentileUs(0.0));
}
