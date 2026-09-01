//==============================================================================
//
//  OvenMediaEngine
//
//  Copyright (c) 2026 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <base/common_types.h>
#include <base/modules/marker/marker.h>
#include <base/ovlibrary/ovlibrary.h>

#include <atomic>
#include <map>
#include <optional>
#include <shared_mutex>

#include "../sample.h"

namespace bmff
{
	// The boundary of the segment currently accumulating, as the policy plans it.
	// All positions are integer microseconds on the shared media clock.
	struct SegmentBoundary
	{
		// Where the segment ends. Meaningful only when the plan was asked with a
		// segment start; without one it carries no position, and a stream whose
		// timeline runs below zero ends its segments there too
		int64_t end_us = -1;

		// Whether end_us is the exact end or the aim to cut at or after
		bool exact = false;

		// The name of the segment being accumulated
		int64_t segment_number = -1;
	};

	// What a completed segment actually was. has_marker is filled at the
	// settlement, from the markers the segment consumed.
	struct CompletedSegment
	{
		int64_t number = -1;
		int64_t start_timestamp_us = 0;
		int64_t duration_us = 0;
		bool has_marker = false;
	};

	// What settling a completion yields for the caller
	struct CompletionResult
	{
		// Markers the completed segment must carry: the ones whose positions it
		// covered, and the ones realized on boundaries decided elsewhere
		std::vector<std::shared_ptr<Marker>> markers;
	};

	// The policy's plan for the chunk to emit now
	struct ChunkPlan
	{
		// How many samples from the front of the buffer to emit; 0 emits nothing
		size_t emit_count = 0;
		// Whether the emitted chunk completes the segment
		bool completes_segment = false;
		// Whether that completion is a timeline break; the storage marks the
		// segment that follows it as a discontinuity point
		bool discontinuity = false;
	};

	// Decides where the segments of a track end and what they carry.
	//
	// The policy owns the segmentation entirely: the numbering, the boundary of
	// the segment being accumulated, what goes out on each pass, and the markers
	// that cut and ride the segments. The packager hands over timing facts and
	// executes the answers; it never decides.
	class SegmentBoundaryPolicy
	{
	public:
		struct Config
		{
			uint64_t segment_duration_ms = 6000;
			double chunk_duration_ms = 500.0;
			// The cadence this track can cut at (0 when it can cut anywhere).
			// A break must be long enough to return from at that cadence.
			double keyframe_interval_ms = 0.0;
			// Where the numbering starts (server-time based numbering derives it
			// from the wall clock)
			int64_t initial_segment_number = 0;
			// Whether a CUE-OUT cuts right at its position or at the next keyframe
			LLHlsCueOutCutMode cue_out_cut_mode = LLHlsCueOutCutMode::Keyframe;
			ov::String log_context;
		};

		explicit SegmentBoundaryPolicy(const Config &config);

		virtual ~SegmentBoundaryPolicy() = default;

		// (input) Whether this track takes markers directly; a policy that
		// follows boundaries decided elsewhere receives them relayed on those
		// boundaries instead
		virtual bool AcceptsMarkers() const
		{
			return true;
		}

		// (input) Validate a marker against the pending chain (OUT opens, IN
		// closes, a break spans at least one cadence). A position already passed
		// is accepted up to one segment late.
		std::tuple<bool, ov::String> CanInsertMarker(const std::shared_ptr<Marker> &marker) const;
		// A validated insert, ready to be applied
		struct PreparedMarker
		{
			std::shared_ptr<Marker> marker;
			// The position the cut lands at
			int64_t cut_us = -1;
			// The pending return point this one takes the place of; negative when
			// it replaces nothing
			int64_t replaces_cut_us = -1;
			// The break this marker belongs to
			std::shared_ptr<Marker> parent;
		};

		// (input) Validate a marker and work out what inserting it would do,
		// without changing anything. A caller inserting the same marker into
		// several tracks prepares all of them first, so it learns that every one
		// accepts before any one of them changes.
		std::optional<PreparedMarker> PrepareMarker(const std::shared_ptr<Marker> &marker) const;


		// (input) Apply a prepared insert. Cannot fail: the position was already
		// decided, and the state it was decided against may only have moved on.
		void CommitMarker(const PreparedMarker &prepared);

		// (input) Insert a marker, preparing and applying in one step. The
		// advertised time stays as received; a late position only moves where the
		// cut lands.
		bool InsertMarker(const std::shared_ptr<Marker> &marker);

		// (input) The timeline breaks at the first cuttable frame at or after
		// the position (a track change on another track). A break within a
		// segment length of the last one handled is the same event, absorbed.
		// Arrives on the media thread, like the media facts.
		void RequestDiscontinuity(int64_t boundary_timestamp_us);

		// (observation) The storage accepted a media chunk. Everything the
		// storage knows about it is passed along, so a policy never has to ask.
		virtual void OnMediaChunk(int64_t start_timestamp_us, int64_t duration_us, bool independent, bool last_chunk)
		{
			(void)start_timestamp_us;
			(void)duration_us;
			(void)independent;
			(void)last_chunk;
		}

		// (decision) The boundary and number of the segment starting at
		// segment_start_us (std::nullopt: only the numbering is meaningful).
		// A pure query, asked repeatedly as the plan firms up.
		virtual SegmentBoundary GetSegmentBoundary(std::optional<int64_t> segment_start_us) = 0;

		// (decision) May a chunk ending at chunk_end_us be emitted now? Only
		// when every boundary up to that position is decided; an emitted chunk
		// cannot be cut retroactively.
		virtual bool CanEmitChunk(int64_t chunk_end_us) const
		{
			(void)chunk_end_us;
			return true;
		}

		// (decision) What goes out now. The packager hands over the facts (the
		// buffered samples, the frame that just arrived, where the accumulating
		// segment starts, and what its stored part already holds) and executes
		// the answer. Only the timing each sample carries is read. Implemented
		// here once, on GetSegmentBoundary, CanEmitChunk and the pending
		// markers, so a policy shapes the emission through those alone.
		ChunkPlan GetChunkPlan(const Samples &buffered,
							   const SampleTiming &next_frame,
							   int64_t segment_start_us,
							   int64_t last_segment_duration_us);

		// (settlement) A segment completed. The markers it covered are consumed
		// here and returned, and the numbering always advances past the settled
		// segment; the policy's own share is the hook below.
		CompletionResult OnSegmentCompleted(const CompletedSegment &completed);

		// (event) A discontinuity (track change, a boundary propagated from
		// another track) closed the timeline here. completed describes the
		// segment force-completed on the spot (number -1 and duration 0 when
		// none was in progress). Settled like a completion: the markers the
		// force-closed segment covered are consumed and returned so they still
		// ride it, and the numbering always advances past it.
		CompletionResult OnDiscontinuity(const CompletedSegment &completed);

	protected:
		// The policy's share of the settlements; the marker and numbering
		// bookkeeping is already taken care of. covered_markers are the pending
		// markers this segment consumed; they are attached to the segment by the
		// base, the hook only relays them further when the policy does that.
		virtual CompletionResult DoOnSegmentCompleted(const CompletedSegment &completed, const std::vector<std::shared_ptr<Marker>> &covered_markers) = 0;
		virtual CompletionResult DoOnDiscontinuity(const CompletedSegment &completed, const std::vector<std::shared_ptr<Marker>> &covered_markers)
		{
			(void)completed;
			(void)covered_markers;
			return {};
		}

		// The newest sample position this track has seen. A policy that has to
		// pace itself while another track stalls measures the lag with it: it
		// advances with every arriving sample, even while nothing may be emitted.
		int64_t GetNewestSampleEndUs() const
		{
			return _newest_sample_end_us.load(std::memory_order_relaxed);
		}

		// Where this track's media started. A lag measured against a track that
		// has reported nothing yet is measured from here: the media clock starts
		// wherever the source is, so measuring from zero would read the very
		// first sample as hours of lag.
		int64_t GetFirstSampleEndUs() const
		{
			return _first_sample_end_us.load(std::memory_order_relaxed);
		}

		int64_t _segment_duration_us = 0;
		int64_t _chunk_duration_us = 0;
		// The cadence this track really cuts at: the segment duration rounded up
		// to whole keyframe intervals. A break must span at least one of it to
		// return from, and a position it has passed by more than one is beyond
		// saving.
		int64_t _cut_cadence_us = 0;
		// The nominal keyframe interval, how far the next cuttable position is
		// expected to be; zero where every frame can cut
		int64_t _keyframe_interval_us = 0;
		LLHlsCueOutCutMode _cue_out_cut_mode = LLHlsCueOutCutMode::Keyframe;
		ov::String _log_context;

		// The name of the segment accumulating now, advanced by the settlements
		int64_t _next_segment_number = 0;

	private:
		// The shared settlement: consume the covered markers, hand them to the
		// policy's hook, and advance the numbering
		CompletionResult Settle(const CompletedSegment &completed, bool discontinuity);

		void AdvanceNumbering(const CompletedSegment &completed)
		{
			if (completed.number >= 0)
			{
				_next_segment_number = completed.number + 1;
			}
		}

		// The same three steps without locking, for a caller already holding
		// _markers_guard. InsertMarker uses them to decide and apply as one.
		std::tuple<bool, ov::String> CanInsertMarkerUnlocked(const std::shared_ptr<Marker> &marker) const;
		std::optional<PreparedMarker> PrepareMarkerUnlocked(const std::shared_ptr<Marker> &marker) const;
		void CommitMarkerUnlocked(const PreparedMarker &prepared);

		// The position the marker's cut lands at, with _markers_guard held; the
		// marker is never modified. A late position is clamped forward, and
		// negative when it is too late even for that.
		int64_t EffectiveCutUs(const std::shared_ptr<Marker> &marker) const;

		// The break length the marker's event declares, -1 when it has none
		static int64_t GetEventDurationMs(const std::shared_ptr<Marker> &marker);

		// The nearest pending cut: the position it lands at and its marker
		std::pair<int64_t, std::shared_ptr<Marker>> GetFirstPendingCut() const;

		// The markers a settlement takes over
		struct SettledMarkers
		{
			// Attached to the segment: the ones it covered, plus the ones whose
			// position it passed within a segment length
			std::vector<std::shared_ptr<Marker>> markers;
			// Whether one of them cut this segment, so the pacing knows this
			// segment gained a boundary
			bool cut_this_segment = false;
		};

		// Consume the markers this settlement takes over. A position the media
		// never reached still rides the next segment, up to a segment length
		// behind; older than that it is discarded.
		SettledMarkers PopMarkersForSettlement(int64_t start_us, int64_t end_us);

		// The markers this track owes cuts to, keyed by the position the cut
		// lands at (µs); the advertised time stays on the marker. Inserted from
		// the request side, consumed on the media side.
		std::map<int64_t, std::shared_ptr<Marker>> _pending_markers;
		std::shared_ptr<Marker> _last_inserted_marker;
		int64_t _last_inserted_cut_us = -1;
		mutable std::shared_mutex _markers_guard;
		// Mirror of _pending_markers.size() so the per-sample paths skip the
		// lock entirely on the common no-marker stream
		std::atomic<size_t> _pending_marker_count{0};

		// The newest sample position this track has seen, the clamp floor for
		// late markers. One media thread writes it today, so the plain type
		// would do; atomic keeps it safe for a reader on another thread, which
		// the marker insertion path becomes when it is driven from an API.
		std::atomic<int64_t> _newest_sample_end_us{-1};

		// The first sample position this track saw; written once, on the same
		// pass and for the same reason as the newest one
		std::atomic<int64_t> _first_sample_end_us{-1};

		// Where a propagated timeline break waits to cut, and where the last one
		// landed (the dedup floor). A track changing configuration requests the
		// cut on the other tracks' policies from the notification thread, so both
		// are read and written across threads.
		std::atomic<int64_t> _pending_discontinuity_cut_us{-1};
		std::atomic<int64_t> _last_discontinuity_us{-1};
	};
}  // namespace bmff
