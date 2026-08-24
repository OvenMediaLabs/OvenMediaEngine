//==============================================================================
//
//  OvenMediaEngine
//
//  Copyright (c) 2026 AirenSoft. All rights reserved.
//
//==============================================================================
#include <gtest/gtest.h>

#include <cmath>

#include <base/modules/data_format/cue_event/cue_event.h>

#include "duration_boundary_policy.h"

// DurationBoundaryPolicy: each segment targets the configured duration, paced
// so the declared timeline keeps up with the configured cadence. A segment that
// came out long makes the next target shorter, a marker segment is discounted
// so the track returns to the same cadence, and a discontinuity realigns the
// pacing (unless server-time numbering relies on it). The policy also owns the
// numbering: it starts from the configured initial number and advances by the
// settlements.

namespace
{
	constexpr uint64_t kSegmentDurationMs = 4000;

	std::shared_ptr<bmff::DurationBoundaryPolicy> MakePolicy(int64_t initial_segment_number = 0, bool wall_clock_slot_pacing = false)
	{
		bmff::SegmentBoundaryPolicy::Config config;
		config.segment_duration_ms = kSegmentDurationMs;
		config.chunk_duration_ms = 400.0;
		config.initial_segment_number = initial_segment_number;
		config.log_context = "test";
		return std::make_shared<bmff::DurationBoundaryPolicy>(config, wall_clock_slot_pacing);
	}

	bmff::CompletionResult Complete(const std::shared_ptr<bmff::DurationBoundaryPolicy> &policy, int64_t number, double start_ms, double duration_ms)
	{
		bmff::CompletedSegment completed_segment;
		completed_segment.number = number;
		completed_segment.start_timestamp_us = std::llround(start_ms * 1000.0);
		completed_segment.duration_us = std::llround(duration_ms * 1000.0);
		return policy->OnSegmentCompleted(completed_segment);
	}

	// The target the policy plans from a segment starting at 0, in milliseconds
	double TargetMs(const std::shared_ptr<bmff::DurationBoundaryPolicy> &policy)
	{
		return static_cast<double>(policy->GetSegmentBoundary(static_cast<int64_t>(0)).end_us) / 1000.0;
	}
}  // namespace

TEST(DurationBoundaryPolicyTest, InitialTargetIsConfiguredDuration)
{
	auto policy = MakePolicy();

	EXPECT_DOUBLE_EQ(TargetMs(policy), 4000.0);
}

TEST(DurationBoundaryPolicyTest, PlanIsRelativeToTheSegmentStart)
{
	auto policy = MakePolicy();

	auto boundary = policy->GetSegmentBoundary(static_cast<int64_t>(12000000));
	EXPECT_EQ(boundary.end_us, 16000000);
	EXPECT_FALSE(boundary.exact);

	// An unknown start plans only the numbering
	EXPECT_LT(policy->GetSegmentBoundary(std::nullopt).end_us, 0);

	// A negative start is a real position, not "unknown"
	EXPECT_EQ(policy->GetSegmentBoundary(static_cast<int64_t>(-100000)).end_us, 3900000);
}

TEST(DurationBoundaryPolicyTest, NumberingStartsAtTheInitialAndFollowsSettlements)
{
	auto policy = MakePolicy(100);

	EXPECT_EQ(policy->GetSegmentBoundary(static_cast<int64_t>(0)).segment_number, 100);

	Complete(policy, 100, 0.0, 4000.0);
	EXPECT_EQ(policy->GetSegmentBoundary(static_cast<int64_t>(4000000)).segment_number, 101);

	// A force-completed segment names the next one too
	bmff::CompletedSegment completed_segment;
	completed_segment.number = 101;
	completed_segment.duration_us = 2000000;
	policy->OnDiscontinuity(completed_segment);
	EXPECT_EQ(policy->GetSegmentBoundary(static_cast<int64_t>(10000000)).segment_number, 102);

	// A discontinuity with nothing in progress changes no numbering
	policy->OnDiscontinuity({});
	EXPECT_EQ(policy->GetSegmentBoundary(static_cast<int64_t>(10000000)).segment_number, 102);
}

TEST(DurationBoundaryPolicyTest, LongSegmentShortensNextTarget)
{
	auto policy = MakePolicy();

	Complete(policy, 0, 0.0, 4200.0);
	EXPECT_DOUBLE_EQ(TargetMs(policy), 3800.0);

	// The shorter segment pays the debt back and the cadence returns
	Complete(policy, 1, 4200.0, 3800.0);
	EXPECT_DOUBLE_EQ(TargetMs(policy), 4000.0);
}

TEST(DurationBoundaryPolicyTest, ShortSegmentStretchesNextTarget)
{
	auto policy = MakePolicy();

	Complete(policy, 0, 0.0, 3900.0);
	EXPECT_DOUBLE_EQ(TargetMs(policy), 4100.0);
}

TEST(DurationBoundaryPolicyTest, MarkerSegmentIsDiscounted)
{
	auto policy = MakePolicy();

	// A real marker: the discount applies when the settlement consumes one
	auto marker = Marker::CreateMarker(cmn::BitstreamFormat::CUE, 1400, 1400, CueEvent::Create(CueEvent::CueType::OUT, 10000, 0, false)->Serialize());
	ASSERT_TRUE(policy->InsertMarker(marker));

	// A marker cut ends the segment early; the extra boundary is discounted so
	// the next segment fills only up to the original cadence position
	auto completion_result = Complete(policy, 0, 0.0, 1400.0);
	ASSERT_EQ(completion_result.markers.size(), 1u);
	EXPECT_EQ(completion_result.markers.front(), marker);
	EXPECT_DOUBLE_EQ(TargetMs(policy), 2600.0);

	Complete(policy, 1, 1400.0, 2600.0);
	EXPECT_DOUBLE_EQ(TargetMs(policy), 4000.0);
}

TEST(DurationBoundaryPolicyTest, MarkerLandingOnTheTargetKeepsTheCadence)
{
	auto policy = MakePolicy();

	// A keyframe-mode cut can land exactly where the cadence was going to cut.
	// No extra boundary appeared, so the step counts as usual and the next
	// target stays on the grid; counting it as an extra boundary would push the
	// following segments past EXT-X-TARGETDURATION.
	auto marker = Marker::CreateMarker(cmn::BitstreamFormat::CUE, 4000, 4000, CueEvent::Create(CueEvent::CueType::OUT, 10000, 0, false)->Serialize());
	ASSERT_TRUE(policy->InsertMarker(marker));

	auto completion_result = Complete(policy, 0, 0.0, 4000.0);
	ASSERT_EQ(completion_result.markers.size(), 1u);
	EXPECT_DOUBLE_EQ(TargetMs(policy), 4000.0);

	Complete(policy, 1, 4000.0, 4000.0);
	EXPECT_DOUBLE_EQ(TargetMs(policy), 4000.0);
}

TEST(DurationBoundaryPolicyTest, SlotPacingCountsAMarkerSegmentAsAFullSlot)
{
	auto policy = MakePolicy(0, true);

	// Server-time numbering: every number is one wall-clock slot, so the marker
	// segment is not discounted and the next segment stretches to finish the
	// slot. Discounting would leave the numbering one slot ahead of the wall
	// clock per marker, and a server started later never pairs with it again.
	auto marker = Marker::CreateMarker(cmn::BitstreamFormat::CUE, 1400, 1400, CueEvent::Create(CueEvent::CueType::OUT, 10000, 0, false)->Serialize());
	ASSERT_TRUE(policy->InsertMarker(marker));

	auto completion_result = Complete(policy, 0, 0.0, 1400.0);
	ASSERT_EQ(completion_result.markers.size(), 1u);
	EXPECT_DOUBLE_EQ(TargetMs(policy), 6600.0);

	Complete(policy, 1, 1400.0, 6600.0);
	EXPECT_DOUBLE_EQ(TargetMs(policy), 4000.0);
}

TEST(DurationBoundaryPolicyTest, DiscontinuityRealignsPacing)
{
	auto policy = MakePolicy();

	Complete(policy, 0, 0.0, 4200.0);
	EXPECT_DOUBLE_EQ(TargetMs(policy), 3800.0);

	// The force-completed segment counts, then the pacing is realigned so the
	// next segment is not stretched to catch up across the break
	bmff::CompletedSegment completed_segment;
	completed_segment.number = 1;
	completed_segment.duration_us = 2000000;
	policy->OnDiscontinuity(completed_segment);
	EXPECT_DOUBLE_EQ(TargetMs(policy), 4000.0);

	Complete(policy, 2, 6200.0, 4000.0);
	EXPECT_DOUBLE_EQ(TargetMs(policy), 4000.0);
}

TEST(DurationBoundaryPolicyTest, ServerTimeNumberingKeepsPacingOverDiscontinuity)
{
	auto policy = MakePolicy(0, true);

	Complete(policy, 0, 0.0, 4200.0);
	EXPECT_DOUBLE_EQ(TargetMs(policy), 3800.0);

	// Server-time based numbering relies on the catch-up pacing to keep segment
	// numbers aligned to the wall clock, so a discontinuity must not reset it
	policy->OnDiscontinuity({});
	EXPECT_DOUBLE_EQ(TargetMs(policy), 3800.0);
}

TEST(DurationBoundaryPolicyTest, ForceCompletedSegmentCountsIntoPacing)
{
	auto policy = MakePolicy(0, true);

	// Server-time numbering: every segment number is one wall-clock slot, so a
	// force-completed segment still counts a full cadence step and the next
	// target stretches to finish its slot
	bmff::CompletedSegment completed_segment;
	completed_segment.number = 0;
	completed_segment.duration_us = 2000000;
	policy->OnDiscontinuity(completed_segment);
	EXPECT_DOUBLE_EQ(TargetMs(policy), 6000.0);

	Complete(policy, 1, 2000.0, 6000.0);
	EXPECT_DOUBLE_EQ(TargetMs(policy), 4000.0);
}
