//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2026 AirenSoft. All rights reserved.
//
//==============================================================================
#include "synced_boundary_policy.h"

#include <cmath>

#include "fmp4_private.h"

namespace bmff
{
	// The packagers cut when the accumulated duration reaches the plan, so a plan
	// must never exceed the true boundary even by a rounding step; one microsecond
	// under is far below a frame and keeps the cut on it
	constexpr int64_t kBoundaryBiasUs = 1;

	//--------------------------------------------------------------------------
	// BoundaryFeed
	//--------------------------------------------------------------------------

	BoundaryFeed::BoundaryFeed(const ov::String &log_context)
		: _log_context(log_context)
	{
	}

	void BoundaryFeed::UpdateHighWaterMark(int64_t timestamp_us)
	{
		// The reference track is the only writer, so a guarded store is enough.
		// Released, so a track that sees the mark also sees the boundaries
		// published before it.
		if (timestamp_us > _high_water_mark_us.load(std::memory_order_relaxed))
		{
			_high_water_mark_us.store(timestamp_us, std::memory_order_release);
		}
	}

	void BoundaryFeed::PublishBoundary(const Boundary &boundary)
	{
		{
			ov::LockGuard lock(_mutex);

			if (_boundaries.empty() == false)
			{
				const auto &last = _boundaries.back();
				if (boundary.segment_number <= last.segment_number || boundary.timestamp_us < last.timestamp_us)
				{
					logtw("%s - Dropped a non-monotonic boundary: segment(%" PRId64 ") at %.3f ms, last segment(%" PRId64 ") at %.3f ms",
						  _log_context.CStr(), boundary.segment_number, boundary.timestamp_us / 1000.0, last.segment_number, last.timestamp_us / 1000.0);
					return;
				}
			}

			_boundaries.push_back(boundary);

			while (_boundaries.size() > kMaxBoundaries)
			{
				_boundaries.pop_front();
			}
		}

		// A boundary is a position the reference has reached
		UpdateHighWaterMark(boundary.timestamp_us);
	}

	int64_t BoundaryFeed::GetHighWaterMarkUs() const
	{
		return _high_water_mark_us.load(std::memory_order_acquire);
	}

	bool BoundaryFeed::HasBoundaries() const
	{
		ov::LockGuard lock(_mutex);
		return _boundaries.empty() == false;
	}

	std::optional<BoundaryFeed::Boundary> BoundaryFeed::GetBoundary(int64_t segment_number) const
	{
		ov::LockGuard lock(_mutex);

		for (const auto &boundary : _boundaries)
		{
			if (boundary.segment_number == segment_number)
			{
				return boundary;
			}
		}

		return std::nullopt;
	}

	std::optional<BoundaryFeed::Boundary> BoundaryFeed::GetBoundaryAfterNumber(int64_t segment_number) const
	{
		ov::LockGuard lock(_mutex);

		for (const auto &boundary : _boundaries)
		{
			if (boundary.segment_number > segment_number)
			{
				return boundary;
			}
		}

		return std::nullopt;
	}

	std::optional<BoundaryFeed::Boundary> BoundaryFeed::GetBoundaryAfterTimestamp(int64_t timestamp_us) const
	{
		ov::LockGuard lock(_mutex);

		for (const auto &boundary : _boundaries)
		{
			if (boundary.timestamp_us > timestamp_us)
			{
				return boundary;
			}
		}

		return std::nullopt;
	}

	std::optional<BoundaryFeed::Boundary> BoundaryFeed::GetNewestBoundaryAtOrBefore(int64_t timestamp_us) const
	{
		ov::LockGuard lock(_mutex);

		for (auto it = _boundaries.rbegin(); it != _boundaries.rend(); ++it)
		{
			if (it->timestamp_us <= timestamp_us)
			{
				return *it;
			}
		}

		return std::nullopt;
	}

	//--------------------------------------------------------------------------
	// ReferenceBoundaryPolicy
	//--------------------------------------------------------------------------

	ReferenceBoundaryPolicy::ReferenceBoundaryPolicy(const Config &config, double video_frame_rate)
		: SegmentBoundaryPolicy(config),
		  _boundary_feed(std::make_shared<BoundaryFeed>(config.log_context)),
		  _video_frame_rate(video_frame_rate)
	{
	}

	std::shared_ptr<const BoundaryFeed> ReferenceBoundaryPolicy::GetBoundaryFeed() const
	{
		return _boundary_feed;
	}

	int64_t ReferenceBoundaryPolicy::NextBoundaryUs(int64_t position_us) const
	{
		// Snap jitter tolerance: a segment that starts exactly on a boundary
		// (within half a frame) belongs to that boundary, not before it
		const int64_t tolerance_us = (_video_frame_rate > 0) ? std::llround(500000.0 / _video_frame_rate) : 500;

		const int64_t origin_us = (_grid_origin_us >= 0) ? _grid_origin_us : 0;

		// The origin itself is a boundary, so the search starts at it; the check
		// below rejects it when the segment already starts there
		int64_t multiple = std::max(static_cast<int64_t>(0), (position_us - origin_us) / _segment_duration_us);

		while (true)
		{
			int64_t boundary_us = origin_us + (multiple * _segment_duration_us);

			// The aimed position must be a real frame position, so the realized
			// cut can land exactly on it when the keyframe cadence allows. The
			// cadence is counted from the origin, where this stream's frames are
			if (_video_frame_rate > 0)
			{
				double frame_index = std::round(static_cast<double>(boundary_us - origin_us) * _video_frame_rate / 1000000.0);
				boundary_us = origin_us + std::llround(frame_index * 1000000.0 / _video_frame_rate);
			}

			if (boundary_us > position_us + tolerance_us)
			{
				return boundary_us;
			}

			multiple++;
		}
	}

	SegmentBoundary ReferenceBoundaryPolicy::GetSegmentBoundary(std::optional<int64_t> segment_start_us)
	{
		SegmentBoundary plan;
		plan.segment_number = _next_segment_number;

		if (segment_start_us.has_value() == false)
		{
			plan.end_us = -1;
			return plan;
		}

		// The first start this reference sees fixes where the aimed positions
		// begin for the stream
		if (_grid_origin_us < 0)
		{
			_grid_origin_us = *segment_start_us;
		}

		plan.end_us = NextBoundaryUs(*segment_start_us) - kBoundaryBiasUs;

		return plan;
	}

	void ReferenceBoundaryPolicy::OnMediaChunk(int64_t start_timestamp_us, int64_t duration_us, bool independent, bool last_chunk)
	{
		(void)independent;
		(void)last_chunk;

		// The first chunk already fixes where and under which number the first
		// segment opened, and that anchor is all a synced track needs to name its
		// own first segment. Waiting for the first completion instead would hold
		// every synced track for a whole segment; the high-water mark still caps
		// how far they may emit.
		if (_opening_anchor_published == false)
		{
			_opening_anchor_published = true;

			if (_boundary_feed->HasBoundaries() == false)
			{
				BoundaryFeed::Boundary opening;
				opening.segment_number = _next_segment_number - 1;
				opening.timestamp_us = start_timestamp_us;
				opening.opening = true;
				_boundary_feed->PublishBoundary(opening);
			}
		}

		// The reference is never held back, so its stored progress is the mark
		// every synced track gates its own chunks on
		_boundary_feed->UpdateHighWaterMark(start_timestamp_us + duration_us);
	}

	CompletionResult ReferenceBoundaryPolicy::DoOnSegmentCompleted(const CompletedSegment &completed, const std::vector<std::shared_ptr<Marker>> &covered_markers)
	{
		BoundaryFeed::Boundary boundary;
		boundary.segment_number = completed.number;
		boundary.timestamp_us = completed.start_timestamp_us + completed.duration_us;
		boundary.markers = covered_markers;

		_boundary_feed->PublishBoundary(boundary);

		// Boundaries are absolute positions; nothing accumulates
		return {};
	}

	CompletionResult ReferenceBoundaryPolicy::DoOnDiscontinuity(const CompletedSegment &completed, const std::vector<std::shared_ptr<Marker>> &covered_markers)
	{
		// A force-closed segment is still a realized boundary; the synced tracks
		// must learn its end and the markers it carried
		if (completed.number < 0)
		{
			return {};
		}

		return DoOnSegmentCompleted(completed, covered_markers);
	}

	//--------------------------------------------------------------------------
	// SyncedBoundaryPolicy
	//--------------------------------------------------------------------------

	SyncedBoundaryPolicy::SyncedBoundaryPolicy(const std::shared_ptr<const ReferenceBoundaryPolicy> &reference_policy,
											   const Config &config)
		: SegmentBoundaryPolicy(config),
		  _boundary_feed(reference_policy != nullptr ? reference_policy->GetBoundaryFeed() : nullptr)
	{
		OV_ASSERT2(_boundary_feed != nullptr);
	}

	bool SyncedBoundaryPolicy::IsInFallback() const
	{
		// Measured on the arriving samples, never on what this track was allowed
		// to emit: the gate caps the emission at the reference's mark, so an
		// emission-based probe could never show the reference falling behind
		int64_t head_us = GetNewestSampleEndUs();
		if (head_us < 0)
		{
			return false;
		}

		// Before the reference reports anything there is no mark to measure
		// against, so this track's own start stands in for it
		int64_t reference_position_us = std::max(_boundary_feed->GetHighWaterMarkUs(), GetFirstSampleEndUs());
		int64_t reference_lag_us = head_us - reference_position_us;
		return reference_lag_us > (_segment_duration_us * kStallThresholdFactor);
	}

	std::optional<int64_t> SyncedBoundaryPolicy::DeriveAnchor(int64_t segment_start_us) const
	{
		auto opened_by = _boundary_feed->GetNewestBoundaryAtOrBefore(segment_start_us);
		if (opened_by.has_value() == true)
		{
			return opened_by->segment_number;
		}

		auto closed_by = _boundary_feed->GetBoundaryAfterTimestamp(segment_start_us);
		if (closed_by.has_value() == true)
		{
			// No segment ended on an opening anchor: media starting before it
			// belongs to the slot it opens, never to a short slot of its own
			if (closed_by->opening == true)
			{
				return closed_by->segment_number;
			}

			return closed_by->segment_number - 1;
		}

		return std::nullopt;
	}

	SegmentBoundary SyncedBoundaryPolicy::GetSegmentBoundary(std::optional<int64_t> segment_start_us)
	{
		SegmentBoundary plan;
		plan.exact = true;

		// The boundary to realize is the one after the anchor, by number, never
		// by a time search: nearest-in-time matching mispairs when boundaries
		// crowd within a segment's length (a marker landing next to a natural
		// cut). The first segment anchors on the interval its start falls into.
		std::optional<int64_t> anchor;
		if (IsAnchored() == true)
		{
			anchor = _last_consumed_segment_number;
		}
		else if (segment_start_us.has_value() == true)
		{
			anchor = DeriveAnchor(*segment_start_us);
		}
		if (anchor.has_value() == false)
		{
			plan.segment_number = _next_segment_number;
		}
		else
		{
			plan.segment_number = *anchor + 1;

			if (_cached_boundary_anchor != *anchor || _cached_next_boundary.has_value() == false)
			{
				_cached_next_boundary = _boundary_feed->GetBoundaryAfterNumber(*anchor);
				_cached_boundary_anchor = *anchor;
			}
			const auto &boundary = _cached_next_boundary;
			if (boundary.has_value() == true)
			{
				// Landing at or past the boundary makes the seam an overlap, which
				// every player reconciles; landing short would leave a gap that
				// permanently stalls a frame-counting audio clock
				plan.end_us = boundary->timestamp_us - kBoundaryBiasUs;
				return plan;
			}
		}

		if (segment_start_us.has_value() == false)
		{
			plan.end_us = -1;
		}
		else if (IsInFallback() == true)
		{
			// The reference stopped; pace this track alone until it returns
			plan.end_us = *segment_start_us + _segment_duration_us;
		}
		else
		{
			// No boundary to aim at yet; the chunk gate keeps the segment from
			// growing past the reference in the meantime
			plan.end_us = std::numeric_limits<int64_t>::max() / 2;
		}

		return plan;
	}

	bool SyncedBoundaryPolicy::CanEmitChunk(int64_t chunk_end_us) const
	{
		// Until the lattice exists, nothing may emit: the first segment's number
		// must come from the lattice, or every later segment inherits the
		// initial guess
		if (IsAnchored() == false && _boundary_feed->HasBoundaries() == false)
		{
			return IsInFallback();
		}

		if (chunk_end_us <= _boundary_feed->GetHighWaterMarkUs())
		{
			if (_fallback_logged.exchange(false) == true)
			{
				logti("%s - The reference track caught up; back to synced segmentation", _log_context.CStr());
			}
			return true;
		}

		if (IsInFallback() == true)
		{
			if (_fallback_logged.exchange(true) == false)
			{
				logtw("%s - The reference track stalled %.0f ms behind; pacing this track alone until it returns",
					  _log_context.CStr(), (GetNewestSampleEndUs() - std::max(_boundary_feed->GetHighWaterMarkUs(), GetFirstSampleEndUs())) / 1000.0);
			}
			return true;
		}

		// Beyond the reference: a boundary may still land inside this chunk
		return false;
	}

	CompletionResult SyncedBoundaryPolicy::DoOnSegmentCompleted(const CompletedSegment &completed, const std::vector<std::shared_ptr<Marker>> &covered_markers)
	{
		(void)covered_markers;

		CompletionResult completion_result;
		int64_t segment_end_us = completed.start_timestamp_us + completed.duration_us;

		int64_t cursor;
		if (IsAnchored() == true)
		{
			cursor = _last_consumed_segment_number;
		}
		else
		{
			auto anchor = DeriveAnchor(completed.start_timestamp_us);
			if (anchor.has_value() == false)
			{
				// A self-paced completion before the lattice exists; the storage's
				// own numbering seeds the consumption while the reference is stalled
				if (IsInFallback() == true)
				{
					_last_consumed_segment_number = completed.number;
				}
				return completion_result;
			}

			cursor = *anchor;
		}

		// Consumption is strictly sequential: this completion realizes the next
		// boundary in numbering order, so two completions can never take the same
		// boundary and crowded boundaries pair off one by one. A cut lands at or
		// past its aim, so the boundary it was aimed at is at or below the cut; a
		// forced, overlong segment may have run past several boundaries and
		// realizes each of them.
		constexpr int64_t kRealizedSlackUs = 100;

		int64_t consumed_count = 0;

		while (true)
		{
			auto boundary = _boundary_feed->GetBoundaryAfterNumber(cursor);
			if (boundary.has_value() == false)
			{
				break;
			}

			if (boundary->timestamp_us > segment_end_us + kRealizedSlackUs)
			{
				break;
			}

			cursor = boundary->segment_number;
			consumed_count++;

			// The markers the reference's segment carried land on this track's
			// segment of the same number
			completion_result.markers.insert(completion_result.markers.end(), boundary->markers.begin(), boundary->markers.end());
		}

		if (consumed_count == 0)
		{
			// The storage published this number, so the numbering must move past
			// it even though no boundary was realized (an early cut on a track
			// change, or self-pacing while the reference is stalled). Leaving it
			// behind makes every later segment collide with what is already out,
			// and the collisions never stop. The boundary this segment did not
			// reach is spent with it, the markers it relays included.
			if (completed.number >= 0 && _last_consumed_segment_number < completed.number)
			{
				_last_consumed_segment_number = completed.number;
			}

			if (IsInFallback() == false)
			{
				logtw("%s - A segment completed at %.3f ms before its boundary was realized; that boundary is skipped, so the markers it carries do not reach this track",
					  _log_context.CStr(), segment_end_us / 1000.0);
			}

			return completion_result;
		}

		if (consumed_count > 1)
		{
			logtw("%s - A completion at %.3f ms realized %" PRId64 " boundaries at once (an overlong segment ran past them)",
				  _log_context.CStr(), segment_end_us / 1000.0, consumed_count);
		}

		_last_consumed_segment_number = cursor;

		return completion_result;
	}

	CompletionResult SyncedBoundaryPolicy::DoOnDiscontinuity(const CompletedSegment &completed, const std::vector<std::shared_ptr<Marker>> &covered_markers)
	{
		if (completed.number < 0)
		{
			return {};
		}

		// The force-closed segment settles like a completion, consuming its
		// boundary and the markers it relays when the reference published it
		auto completion_result = DoOnSegmentCompleted(completed, covered_markers);

		// A force-close with no lattice to anchor on still spends its number; a
		// completion that realized no boundary spent its own above
		if (IsAnchored() == false || _last_consumed_segment_number < completed.number)
		{
			_last_consumed_segment_number = completed.number;
		}

		return completion_result;
	}
}  // namespace bmff
