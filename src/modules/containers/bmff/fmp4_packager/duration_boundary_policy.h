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
		// keep_pacing_over_discontinuity: server-time based segment numbering
		// relies on the catch-up pacing to keep segment numbers aligned to the
		// wall clock, so a discontinuity must not reset the drift
		DurationBoundaryPolicy(const Config &config, bool keep_pacing_over_discontinuity);

		SegmentBoundary GetSegmentBoundary(std::optional<int64_t> segment_start_us) override;

	protected:
		CompletionResult DoOnSegmentCompleted(const CompletedSegment &completed, const std::vector<std::shared_ptr<Marker>> &covered_markers) override;
		CompletionResult DoOnDiscontinuity(const CompletedSegment &completed, const std::vector<std::shared_ptr<Marker>> &covered_markers) override;

	private:
		bool _keep_pacing_over_discontinuity = false;

		// The pacing ledger: the ideal timeline advances by the configured
		// duration per segment, the actual one by what each segment really was;
		// their difference shortens or stretches the next target
		int64_t _target_segment_duration_us = 0;
		int64_t _total_segment_duration_us = 0;
		int64_t _total_expected_duration_us = 0;
	};
}  // namespace bmff
