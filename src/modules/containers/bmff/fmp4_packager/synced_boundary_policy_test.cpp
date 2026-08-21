//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2026 AirenSoft. All rights reserved.
//
//==============================================================================
#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>

#include "synced_boundary_policy.h"

// Synced segmentation: the reference track's policy realizes the boundaries
// and publishes them to its feed, every synced track's policy cuts at
// those boundaries, adopts their numbering, reports them realized, and never
// emits a chunk beyond the reference's high-water mark.

namespace
{
	constexpr uint64_t kSegmentDurationMs = 4000;

	bmff::SegmentBoundaryPolicy::Config MakeConfig(int64_t initial_segment_number, const ov::String &log_context)
	{
		bmff::SegmentBoundaryPolicy::Config config;
		config.segment_duration_ms = kSegmentDurationMs;
		config.chunk_duration_ms = 400.0;
		config.initial_segment_number = initial_segment_number;
		config.log_context = log_context;
		return config;
	}

	std::shared_ptr<bmff::ReferenceBoundaryPolicy> MakeReference(double video_frame_rate = 0.0, int64_t initial_segment_number = 0)
	{
		return std::make_shared<bmff::ReferenceBoundaryPolicy>(MakeConfig(initial_segment_number, "test-reference"), video_frame_rate);
	}

	std::shared_ptr<bmff::SyncedBoundaryPolicy> MakeSynced(const std::shared_ptr<bmff::ReferenceBoundaryPolicy> &reference, int64_t initial_segment_number = 0)
	{
		return std::make_shared<bmff::SyncedBoundaryPolicy>(reference, MakeConfig(initial_segment_number, "test-synced"));
	}

	bmff::BoundaryFeed::Boundary MakeBoundary(int64_t number, double timestamp_ms)
	{
		bmff::BoundaryFeed::Boundary boundary;
		boundary.segment_number = number;
		boundary.timestamp_us = std::llround(timestamp_ms * 1000.0);
		return boundary;
	}

	// A completion covering [start, start + duration)
	bmff::CompletionResult Complete(const std::shared_ptr<bmff::SegmentBoundaryPolicy> &policy, int64_t number, double start_ms, double duration_ms)
	{
		bmff::CompletedSegment completed_segment;
		completed_segment.number = number;
		completed_segment.start_timestamp_us = std::llround(start_ms * 1000.0);
		completed_segment.duration_us = std::llround(duration_ms * 1000.0);
		return policy->OnSegmentCompleted(completed_segment);
	}

	// The reference realizes the boundary closing the given segment number at
	// timestamp_ms
	void RealizeBoundary(const std::shared_ptr<bmff::ReferenceBoundaryPolicy> &reference, int64_t number, double timestamp_ms)
	{
		Complete(reference, number, 0.0, timestamp_ms);
	}

	// A sample arriving on this track. The plan pass is where the policy sees
	// its position, and that position is what the stall probe measures.
	void Arrive(const std::shared_ptr<bmff::SegmentBoundaryPolicy> &policy, double dts_ms, double duration_ms = 20.0)
	{
		bmff::Samples nothing_buffered;
		bmff::SampleTiming timing;
		timing.dts_us = std::llround(dts_ms * 1000.0);
		timing.pts_us = timing.dts_us;
		timing.duration_us = std::llround(duration_ms * 1000.0);
		timing.independent = true;

		policy->GetChunkPlan(nothing_buffered, timing, 0, 0);
	}

	// The plan for a segment starting at the given position (ms)
	bmff::SegmentBoundary PlanAt(const std::shared_ptr<bmff::SegmentBoundaryPolicy> &policy, double start_ms)
	{
		return policy->GetSegmentBoundary(std::llround(start_ms * 1000.0));
	}
}  // namespace

//------------------------------------------------------------------------------
// BoundaryFeed
//------------------------------------------------------------------------------

TEST(BoundaryFeedTest, HighWaterMarkIsMonotonic)
{
	bmff::BoundaryFeed feed("test-feed");

	EXPECT_LT(feed.GetHighWaterMarkUs(), 0);

	feed.UpdateHighWaterMark(1000000);
	EXPECT_EQ(feed.GetHighWaterMarkUs(), 1000000);

	// An older position never pulls it back
	feed.UpdateHighWaterMark(500000);
	EXPECT_EQ(feed.GetHighWaterMarkUs(), 1000000);
}

TEST(BoundaryFeedTest, PublishRejectsNonMonotonicBoundaries)
{
	bmff::BoundaryFeed feed("test-feed");

	feed.PublishBoundary(MakeBoundary(10, 4000.0));

	// Same number, and a rewound timestamp: both dropped
	feed.PublishBoundary(MakeBoundary(10, 8000.0));
	feed.PublishBoundary(MakeBoundary(11, 3000.0));

	auto boundary = feed.GetBoundaryAfterTimestamp(0);
	ASSERT_TRUE(boundary.has_value());
	EXPECT_EQ(boundary->segment_number, 10);

	EXPECT_FALSE(feed.GetBoundaryAfterNumber(10).has_value());
}

TEST(BoundaryFeedTest, PublishLiftsTheHighWaterMark)
{
	bmff::BoundaryFeed feed("test-feed");

	feed.PublishBoundary(MakeBoundary(0, 4000.0));
	EXPECT_EQ(feed.GetHighWaterMarkUs(), 4000000);
}

TEST(BoundaryFeedTest, QueriesWalkTheLattice)
{
	bmff::BoundaryFeed feed("test-feed");
	feed.PublishBoundary(MakeBoundary(0, 4000.0));
	feed.PublishBoundary(MakeBoundary(1, 8000.0));
	feed.PublishBoundary(MakeBoundary(2, 12000.0));

	EXPECT_EQ(feed.GetBoundaryAfterTimestamp(4000000)->segment_number, 1);
	EXPECT_EQ(feed.GetBoundaryAfterTimestamp(3999000)->segment_number, 0);
	EXPECT_FALSE(feed.GetBoundaryAfterTimestamp(12000000).has_value());

	EXPECT_EQ(feed.GetNewestBoundaryAtOrBefore(8000000)->segment_number, 1);
	EXPECT_EQ(feed.GetNewestBoundaryAtOrBefore(7999000)->segment_number, 0);
	EXPECT_FALSE(feed.GetNewestBoundaryAtOrBefore(3999000).has_value());

	EXPECT_EQ(feed.GetBoundaryAfterNumber(0)->segment_number, 1);
	EXPECT_FALSE(feed.GetBoundaryAfterNumber(2).has_value());

	EXPECT_EQ(feed.GetBoundary(1)->timestamp_us, 8000000);
	EXPECT_FALSE(feed.GetBoundary(3).has_value());
}

//------------------------------------------------------------------------------
// SyncedBoundaryPolicy
//------------------------------------------------------------------------------

TEST(SyncedBoundaryPolicyTest, PlanAimsAtTheNextBoundary)
{
	auto reference = MakeReference();
	auto policy = MakeSynced(reference);

	RealizeBoundary(reference, 0, 4000.0);

	// One microsecond under the boundary, the same bias as the reference's
	// aim; the boundary is an absolute position, wherever the segment starts
	auto plan = PlanAt(policy, 0.0);
	EXPECT_NEAR(plan.end_us, 4000000.0, 10.0);
	EXPECT_TRUE(plan.exact);
	EXPECT_NEAR(PlanAt(policy, 1500.0).end_us, 4000000.0, 10.0);
}

TEST(SyncedBoundaryPolicyTest, HoldsWithoutABoundary)
{
	auto policy = MakeSynced(MakeReference());

	// Nothing realized yet: the boundary is unreachable and chunks are held
	EXPECT_GT(PlanAt(policy, 0.0).end_us, 1000000000);
	EXPECT_FALSE(policy->CanEmitChunk(800000));
}

TEST(SyncedBoundaryPolicyTest, ChunkGateFollowsTheHighWaterMark)
{
	auto reference = MakeReference();
	auto policy = MakeSynced(reference);

	RealizeBoundary(reference, 0, 4000.0);
	// The reference appended up to 5000 ms
	EXPECT_TRUE(reference->CanEmitChunk(5000000));

	EXPECT_TRUE(policy->CanEmitChunk(4800000));
	EXPECT_TRUE(policy->CanEmitChunk(5000000));
	EXPECT_FALSE(policy->CanEmitChunk(5200000));
}

TEST(SyncedBoundaryPolicyTest, StalledReferenceFallsBackToSelfPacing)
{
	auto reference = MakeReference();
	auto policy = MakeSynced(reference);

	// The reference appended up to 1000 ms, then stalled
	EXPECT_TRUE(reference->CanEmitChunk(1000000));

	// The samples of this track keep arriving from where the reference stopped.
	// The lag is measured on them: the gate holds every emission at the
	// reference's mark, so what this track was allowed to emit can never show
	// the reference falling behind.
	Arrive(policy, 1000.0);
	Arrive(policy, 1000.0 + kSegmentDurationMs);
	EXPECT_FALSE(policy->IsInFallback());
	EXPECT_FALSE(policy->CanEmitChunk(1000000 + static_cast<int64_t>(kSegmentDurationMs) * 1000));

	// Past the threshold: the gate opens and the track paces itself
	Arrive(policy, 1000.0 + kSegmentDurationMs * 2 + 100);
	EXPECT_TRUE(policy->IsInFallback());
	EXPECT_TRUE(policy->CanEmitChunk(1000000 + static_cast<int64_t>(kSegmentDurationMs) * 1000));
	EXPECT_EQ(PlanAt(policy, 1000.0).end_us, 1000000 + static_cast<int64_t>(kSegmentDurationMs) * 1000);
}

TEST(SyncedBoundaryPolicyTest, AnAbsoluteMediaClockIsNotMistakenForAStall)
{
	auto reference = MakeReference();
	auto policy = MakeSynced(reference);

	// The media clock starts wherever the source is: an hour in here, and the
	// reference has not reported anything yet
	Arrive(policy, 3600000.0);
	EXPECT_FALSE(policy->IsInFallback());

	// A segment of samples later it is still not a stall
	Arrive(policy, 3604000.0);
	EXPECT_FALSE(policy->IsInFallback());

	// Two segments without a word from the reference is
	Arrive(policy, 3608100.0);
	EXPECT_TRUE(policy->IsInFallback());
}

TEST(SyncedBoundaryPolicyTest, ACompletionWithoutItsBoundarySpendsTheNumber)
{
	auto reference = MakeReference();
	auto policy = MakeSynced(reference);

	// The first boundary names this track's first segment
	RealizeBoundary(reference, 10, 4000.0);
	EXPECT_EQ(PlanAt(policy, 0.0).segment_number, 10);
	Complete(policy, 10, 0.0, 4000.0);

	// The next segment is cut early, before the reference realized the boundary
	// that was to close it (a flush on this track's own configuration change)
	EXPECT_EQ(PlanAt(policy, 4000.0).segment_number, 11);
	Complete(policy, 11, 4000.0, 1000.0);

	// The number went out with that segment, so it is spent: reusing it would
	// collide with what the storage already published, and every later segment
	// with it
	EXPECT_EQ(PlanAt(policy, 5000.0).segment_number, 12);

	// The skipped boundary arrives late. It is spent too, so the numbering stays
	// with the reference instead of running a segment ahead of it forever.
	RealizeBoundary(reference, 11, 8000.0);
	RealizeBoundary(reference, 12, 12000.0);
	EXPECT_EQ(PlanAt(policy, 5000.0).segment_number, 12);
	EXPECT_EQ(PlanAt(policy, 5000.0).end_us, 12000000 - 1);

	Complete(policy, 12, 5000.0, 7000.0);
	EXPECT_EQ(PlanAt(policy, 12000.0).segment_number, 13);
}

TEST(SyncedBoundaryPolicyTest, NumbersComeFromTheLattice)
{
	auto reference = MakeReference();
	RealizeBoundary(reference, 10, 4000.0);
	RealizeBoundary(reference, 11, 8000.0);

	{
		// No boundary realized yet: the initial numbering stands, unanchored
		auto policy = MakeSynced(MakeReference(), 100);
		EXPECT_EQ(PlanAt(policy, 3500.0).segment_number, 100);
	}

	{
		// A track starting before the first boundary: that boundary closes its
		// first segment, so the segment takes its number
		auto policy = MakeSynced(reference);
		EXPECT_EQ(PlanAt(policy, 3500.0).segment_number, 10);
	}

	{
		// A track starting after a boundary skips the slots it never covered
		auto policy = MakeSynced(reference);
		EXPECT_EQ(PlanAt(policy, 4200.0).segment_number, 11);
	}

	{
		auto policy = MakeSynced(reference);
		EXPECT_EQ(PlanAt(policy, 8000.0).segment_number, 12);
	}

	{
		// Consuming a boundary names the next segment, also the pre-created one
		// that has no first chunk yet; the storage's own counting never re-enters
		auto policy = MakeSynced(reference);
		EXPECT_EQ(PlanAt(policy, 3500.0).segment_number, 10);
		Complete(policy, 10, 3500.0, 520.0);
		EXPECT_EQ(policy->GetSegmentBoundary(std::nullopt).segment_number, 11);
	}
}

TEST(SyncedBoundaryPolicyTest, ConsumptionAdvancesTheNumbering)
{
	auto reference = MakeReference();
	auto policy = MakeSynced(reference);

	RealizeBoundary(reference, 0, 4000.0);

	// An audio completion overshoots the boundary by a frame and still
	// consumes it; the next segment takes the next lattice number
	Complete(policy, 0, 3500.0, 521.0);
	EXPECT_EQ(policy->GetSegmentBoundary(std::nullopt).segment_number, 1);
}

TEST(SyncedBoundaryPolicyTest, CrowdedBoundariesAreConsumedSequentially)
{
	auto reference = MakeReference();
	auto policy = MakeSynced(reference);

	// A marker cut landing next to a natural cut: two boundaries 100 ms apart
	RealizeBoundary(reference, 10, 4000.0);
	RealizeBoundary(reference, 11, 4100.0);

	EXPECT_EQ(PlanAt(policy, 3000.0).segment_number, 10);

	// Two quick completions, both within 100 ms of both boundaries: each
	// consumes exactly one, in order, and the numbering never repeats
	Complete(policy, 10, 3000.0, 1010.0);
	EXPECT_EQ(policy->GetSegmentBoundary(std::nullopt).segment_number, 11);
	Complete(policy, 11, 4010.0, 100.0);
	EXPECT_EQ(policy->GetSegmentBoundary(std::nullopt).segment_number, 12);
}

TEST(SyncedBoundaryPolicyTest, AnOverlongCompletionRealizesEveryBoundaryItPassed)
{
	auto reference = MakeReference();
	auto policy = MakeSynced(reference);

	RealizeBoundary(reference, 10, 4000.0);
	RealizeBoundary(reference, 11, 8000.0);

	EXPECT_EQ(PlanAt(policy, 3000.0).segment_number, 10);

	// A forced cut that ran past both boundaries consumes them together
	Complete(policy, 10, 3000.0, 5010.0);
	EXPECT_EQ(policy->GetSegmentBoundary(std::nullopt).segment_number, 12);
}

//------------------------------------------------------------------------------
// ReferenceBoundaryPolicy
//------------------------------------------------------------------------------

TEST(ReferenceBoundaryPolicyTest, BoundariesSnapToTheFrameCadence)
{
	// 29.97 fps: the frame period is 1001/30 ms, so the aimed positions are
	// not integer multiples of the segment duration
	const double frame_period_us = 1001000.0 / 30.0;
	auto policy = MakeReference(30000.0 / 1001.0);

	const int64_t first = policy->NextBoundaryUs(0);
	EXPECT_NEAR(first, 120 * frame_period_us, 1.0);  // 4004.0 ms

	const int64_t second = policy->NextBoundaryUs(first);
	EXPECT_NEAR(second, 240 * frame_period_us, 1.0);  // 8008.0 ms

	// Every position sits exactly on a frame
	EXPECT_NEAR(std::remainder(static_cast<double>(first), frame_period_us), 0.0, 1.0);
}

TEST(ReferenceBoundaryPolicyTest, BoundaryFollowsTheSegmentStart)
{
	auto policy = MakeReference(30.0, 7);

	// Unknown start: only the numbering is planned. The planned boundaries sit
	// a microsecond under the true position so a rounding ulp cannot push the
	// cut past the intended frame.
	auto unknown = policy->GetSegmentBoundary(std::nullopt);
	EXPECT_LT(unknown.end_us, 0);
	EXPECT_EQ(unknown.segment_number, 7);
	EXPECT_FALSE(unknown.exact);

	// A segment starting on a boundary targets the next one
	EXPECT_NEAR(PlanAt(policy, 4000.0).end_us, 8000000.0, 10.0);

	// A segment starting at a marker cut targets the same absolute position,
	// so the track returns to the shared cadence by itself
	EXPECT_NEAR(PlanAt(policy, 5500.0).end_us, 8000000.0, 10.0);

	// A boundary right after the segment start is still honored
	EXPECT_NEAR(PlanAt(policy, 3900.0).end_us, 4000000.0, 10.0);
}

TEST(ReferenceBoundaryPolicyTest, PublishesRealizedBoundaries)
{
	auto policy = MakeReference();

	// A marker cut completed the segment early, at 1400
	Complete(policy, 5, 0.0, 1400.0);

	auto boundary = policy->GetBoundaryFeed()->GetBoundaryAfterTimestamp(0);
	ASSERT_TRUE(boundary.has_value());
	EXPECT_EQ(boundary->segment_number, 5);
	EXPECT_EQ(boundary->timestamp_us, 1400000);

	// The next segment starts at the cut, takes the next number, and aims at
	// the same absolute position, so the track returns to the shared cadence
	// by itself
	EXPECT_NEAR(PlanAt(policy, 1400.0).end_us, 4000000.0, 10.0);
	EXPECT_EQ(PlanAt(policy, 1400.0).segment_number, 6);
}

TEST(ReferenceBoundaryPolicyTest, AppendProgressLiftsTheHighWaterMark)
{
	auto policy = MakeReference();

	EXPECT_TRUE(policy->CanEmitChunk(1234000));
	EXPECT_EQ(policy->GetBoundaryFeed()->GetHighWaterMarkUs(), 1234000);
}

TEST(SyncedBoundaryPolicyTest, DiscontinuityIsSettledLikeACompletion)
{
	auto reference = MakeReference();
	auto synced = MakeSynced(reference);

	// The reference force-closes segment 0 at 3000 ms; the boundary is still
	// published, so the synced track can aim at it
	bmff::CompletedSegment forced;
	forced.number = 0;
	forced.start_timestamp_us = 0;
	forced.duration_us = 3000000;
	reference->OnDiscontinuity(forced);

	EXPECT_NEAR(PlanAt(synced, 0.0).end_us, 3000000.0, 10.0);

	// The synced track is force-closed on the propagated cut; the anchor moves
	// past the spent number so the next plan does not reissue it
	synced->OnDiscontinuity(forced);
	EXPECT_EQ(PlanAt(synced, 3000.0).segment_number, 1);
}
