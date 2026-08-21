//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2026 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <atomic>
#include <deque>
#include <limits>
#include <optional>

#include "segment_boundary_policy.h"

namespace bmff
{
	// The realized segment boundaries of a stream, written by the reference
	// track's boundary policy and read by every synced track's policy. All
	// positions are on the shared media clock in integer microseconds.
	//
	// The reference publishes a boundary only at its own append position, so a
	// boundary can never appear at or below an already observed high-water mark;
	// a synced track that holds its chunks up to the high-water mark therefore
	// never finds a boundary inside a chunk it already emitted.
	class BoundaryFeed
	{
	public:
		struct Boundary
		{
			// The segment this boundary CLOSES; the next segment is number + 1
			int64_t segment_number = -1;
			int64_t timestamp_us = -1;
			// The markers the reference's segment consumed; every synced track
			// attaches them to its own segment of the same number
			std::vector<std::shared_ptr<Marker>> markers;
		};

		explicit BoundaryFeed(const ov::String &log_context);

		// Reference side
		// The reference track's newest appended sample position, monotonic
		void UpdateHighWaterMark(int64_t timestamp_us);
		// A realized cut; segment_number and timestamp_us must both advance
		void PublishBoundary(const Boundary &boundary);

		// Synced side
		int64_t GetHighWaterMarkUs() const;
		bool HasBoundaries() const;
		// The boundary closing exactly this segment number
		std::optional<Boundary> GetBoundary(int64_t segment_number) const;
		// The earliest boundary closing a segment after the given number
		std::optional<Boundary> GetBoundaryAfterNumber(int64_t segment_number) const;
		// The earliest boundary strictly after the given position
		std::optional<Boundary> GetBoundaryAfterTimestamp(int64_t timestamp_us) const;
		// The newest boundary at or before the given position
		std::optional<Boundary> GetNewestBoundaryAtOrBefore(int64_t timestamp_us) const;

	private:
		// Boundaries a synced track has not consumed yet stay useful only while
		// it lags behind; anything older than this window is unreachable
		static constexpr size_t kMaxBoundaries = 64;

		ov::String _log_context;

		// The reference track writes it, the synced tracks read it. All of them
		// run on one media thread today, so the plain type would do; atomic
		// keeps the feed safe if they are ever driven separately.
		std::atomic<int64_t> _high_water_mark_us{-1};

		mutable ov::Mutex _mutex;
		std::deque<Boundary> _boundaries;
	};

	// The reference track's policy in synced segmentation. It aims each cut at
	// fixed timeline positions (multiples of the configured duration snapped to
	// its own frame cadence), lands on the first cuttable frame at or after
	// them, and publishes every realized boundary to the feed every synced
	// track follows. The fixed positions make the grid deterministic, and a
	// marker cut re-anchors simply by the next segment starting where it starts.
	class ReferenceBoundaryPolicy : public SegmentBoundaryPolicy
	{
	public:
		// video_frame_rate: the cadence the aimed positions snap to, so every
		// position is a real frame (0 keeps plain multiples). The feed the
		// realized boundaries are published to is created and owned here; the
		// synced tracks read it through GetBoundaryFeed().
		ReferenceBoundaryPolicy(const Config &config, double video_frame_rate);

		SegmentBoundary GetSegmentBoundary(std::optional<int64_t> segment_start_us) override;

		// Every stored chunk lifts the high-water mark the synced tracks gate on.
		// Taken from the stored chunk and not from the planned one: an immediate
		// marker cut trims the plan after the gate was consulted, and a mark past
		// the boundary that cut publishes would let a synced track emit past it.
		void OnMediaChunk(int64_t start_timestamp_us, int64_t duration_us, bool independent, bool last_chunk) override;

		// The feed the synced policies of the stream follow; created and owned
		// by this reference
		std::shared_ptr<const BoundaryFeed> GetBoundaryFeed() const;

		// The first aimed position at or after the given one (µs), snapped to
		// the frame cadence. Public for tests.
		int64_t NextBoundaryUs(int64_t position_us) const;

	protected:
		CompletionResult DoOnSegmentCompleted(const CompletedSegment &completed, const std::vector<std::shared_ptr<Marker>> &covered_markers) override;
		CompletionResult DoOnDiscontinuity(const CompletedSegment &completed, const std::vector<std::shared_ptr<Marker>> &covered_markers) override;

	private:
		std::shared_ptr<BoundaryFeed> _boundary_feed;
		double _video_frame_rate = 0;
	};

	// A synced track's policy: computes no boundaries of its own. Each segment
	// ends at the next boundary the reference realized, adopts that boundary's
	// segment number, and reports it realized at completion. Chunks never emit
	// beyond the reference's high-water mark, so a boundary can never land
	// inside a chunk that is already out.
	class SyncedBoundaryPolicy : public SegmentBoundaryPolicy
	{
	public:
		// initial_segment_number seeds the numbering only while nothing anchors
		// it on the lattice (a stalled reference before the first boundary)
		SyncedBoundaryPolicy(const std::shared_ptr<const ReferenceBoundaryPolicy> &reference_policy,
							 const Config &config);

		SegmentBoundary GetSegmentBoundary(std::optional<int64_t> segment_start_us) override;
		bool CanEmitChunk(int64_t chunk_end_us) const override;

		// A synced track receives its markers relayed on the boundaries the
		// reference realized
		bool AcceptsMarkers() const override
		{
			return false;
		}

		// Whether the reference stopped publishing and this track is pacing
		// itself in the meantime. Public for tests.
		bool IsInFallback() const;

	protected:
		CompletionResult DoOnSegmentCompleted(const CompletedSegment &completed, const std::vector<std::shared_ptr<Marker>> &covered_markers) override;
		CompletionResult DoOnDiscontinuity(const CompletedSegment &completed, const std::vector<std::shared_ptr<Marker>> &covered_markers) override;

	private:
		// The reference is considered stalled when this track's samples run this
		// many segment lengths past the reference's high-water mark; segmentation
		// then falls back to self-pacing until the reference catches up
		static constexpr int64_t kStallThresholdFactor = 2;

		// The numbering is not yet anchored on the boundary lattice. A distinct
		// sentinel, because any real number (including -1, one before a lattice
		// starting at 0) is a valid anchor.
		static constexpr int64_t kUnanchored = std::numeric_limits<int64_t>::min();

		bool IsAnchored() const
		{
			return _last_consumed_segment_number != kUnanchored;
		}

		// The boundary lattice interval the given position falls into, as the
		// number of the boundary that opens it; nullopt while the feed is
		// empty. Stateless: the anchor is committed only when a completion
		// consumes a boundary, so a plan asked early never fixes a stale anchor.
		std::optional<int64_t> DeriveAnchor(int64_t segment_start_us) const;

		std::shared_ptr<const BoundaryFeed> _boundary_feed;

		mutable std::atomic<bool> _fallback_logged{false};

		// The segment number of the newest consumed boundary. Consumption is
		// strictly sequential: the next boundary to realize is always the one
		// after this, never a nearest-in-time guess.
		int64_t _last_consumed_segment_number = kUnanchored;

		// The feed answer for the current anchor, cached because it is asked on
		// every sample and a published boundary never changes. A missing answer
		// is retried, so a boundary published later is picked up. Media-thread
		// only, like GetSegmentBoundary itself.
		mutable std::optional<BoundaryFeed::Boundary> _cached_next_boundary;
		mutable int64_t _cached_boundary_anchor = kUnanchored;
	};
}  // namespace bmff
