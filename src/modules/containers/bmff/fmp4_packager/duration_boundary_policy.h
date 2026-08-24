//==============================================================================
//
//  OvenMediaEngine
//
//  Copyright (c) 2026 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include "segment_boundary_policy.h"

namespace bmff
{
	// The default policy: each segment targets the configured duration, paced so
	// the total declared timeline keeps up with the configured cadence: a
	// segment that came out long makes the next target shorter. A marker segment
	// is discounted so every track gains exactly one boundary per marker and
	// returns to the same segment cadence, keeping sequence numbers aligned.
	class DurationBoundaryPolicy : public SegmentBoundaryPolicy
	{
	public:
		// wall_clock_slot_pacing: server-time based segment numbering treats
		// every segment number as one wall-clock slot, so every completion counts
		// a full cadence step (no marker discount, no reset on a discontinuity)
		// and the catch-up pacing finishes each slot. Otherwise a server started
		// later derives its numbers fresh from the wall clock and never matches
		// the ones already running.
		DurationBoundaryPolicy(const Config &config, bool wall_clock_slot_pacing);

		SegmentBoundary GetSegmentBoundary(std::optional<int64_t> segment_start_us) override;

	protected:
		CompletionResult DoOnSegmentCompleted(const CompletedSegment &completed, const std::vector<std::shared_ptr<Marker>> &covered_markers) override;
		CompletionResult DoOnDiscontinuity(const CompletedSegment &completed, const std::vector<std::shared_ptr<Marker>> &covered_markers) override;

	private:
		bool _wall_clock_slot_pacing = false;

		// The pacing ledger: the ideal timeline advances by the configured
		// duration per segment, the actual one by what each segment really was;
		// their difference shortens or stretches the next target
		int64_t _target_segment_duration_us = 0;
		int64_t _total_segment_duration_us = 0;
		int64_t _total_expected_duration_us = 0;
	};
}  // namespace bmff
