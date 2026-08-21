//==============================================================================
//
//  OvenMediaEngine
//
//  Copyright (c) 2026 AirenSoft. All rights reserved.
//
//==============================================================================
#include "duration_boundary_policy.h"

#include "fmp4_private.h"

namespace bmff
{
	DurationBoundaryPolicy::DurationBoundaryPolicy(const Config &config, bool keep_pacing_over_discontinuity)
		: SegmentBoundaryPolicy(config),
		  _keep_pacing_over_discontinuity(keep_pacing_over_discontinuity),
		  _target_segment_duration_us(static_cast<int64_t>(config.segment_duration_ms) * 1000)
	{
	}

	SegmentBoundary DurationBoundaryPolicy::GetSegmentBoundary(std::optional<int64_t> segment_start_us)
	{
		SegmentBoundary boundary;

		// Paced by durations alone: the boundary is a length from wherever the
		// segment starts, landing on the first cuttable frame past it. With the
		// start not known yet only the numbering is meaningful.
		boundary.end_us = segment_start_us.has_value() ? (*segment_start_us + _target_segment_duration_us) : -1;
		boundary.exact = false;
		boundary.segment_number = _next_segment_number;
		boundary.publish_offset_us = 0;

		return boundary;
	}

	CompletionResult DurationBoundaryPolicy::DoOnSegmentCompleted(const CompletedSegment &completed, const std::vector<std::shared_ptr<Marker>> &covered_markers)
	{
		(void)covered_markers;

		// A marker cut that ended the segment before its target added a boundary
		// the cadence did not plan for. That boundary must not count as a step
		// of the cadence, or the ledger would stretch the following segments to
		// catch up with a step that never happened. A marker cut that landed on
		// the target (or past it) added no boundary, so it counts as usual.
		const bool added_a_boundary = (completed.has_marker == true &&
									   completed.duration_us < _target_segment_duration_us);

		_total_expected_duration_us += _segment_duration_us;
		_total_segment_duration_us += completed.duration_us;

		if (added_a_boundary == true)
		{
			_total_expected_duration_us -= _segment_duration_us;
		}

		_target_segment_duration_us = _total_expected_duration_us - _total_segment_duration_us + _segment_duration_us;

		logtd("%s - Segment completed: duration(%.3f ms) expected(%.3f ms) total(%.3f ms) next target(%.3f ms) marker(%d) added_boundary(%d)",
			  _log_context.CStr(), completed.duration_us / 1000.0, _total_expected_duration_us / 1000.0, _total_segment_duration_us / 1000.0, _target_segment_duration_us / 1000.0, completed.has_marker, added_a_boundary);

		return {};
	}

	CompletionResult DurationBoundaryPolicy::DoOnDiscontinuity(const CompletedSegment &completed, const std::vector<std::shared_ptr<Marker>> &covered_markers)
	{
		if (_keep_pacing_over_discontinuity == true)
		{
			// Server-time numbering: every segment number is one wall-clock slot,
			// so even a force-completed segment counts a full cadence step and
			// the next target stretches to finish its slot
			if (completed.number >= 0)
			{
				return DoOnSegmentCompleted(completed, covered_markers);
			}

			return {};
		}

		// The segment closed on the spot still happened
		_total_segment_duration_us += completed.duration_us;

		// The timeline broke here; forgetting the drift keeps the next segment
		// from being stretched past EXT-X-TARGETDURATION to catch up
		_total_expected_duration_us = _total_segment_duration_us;
		_target_segment_duration_us = _segment_duration_us;

		return {};
	}
}  // namespace bmff
