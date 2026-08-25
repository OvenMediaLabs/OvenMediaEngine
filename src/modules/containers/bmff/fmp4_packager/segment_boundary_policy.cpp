//==============================================================================
//
//  OvenMediaEngine
//
//  Copyright (c) 2026 AirenSoft. All rights reserved.
//
//==============================================================================
#include "segment_boundary_policy.h"

#include <algorithm>
#include <cmath>

#include "fmp4_private.h"

namespace bmff
{
	// A settlement consumes the markers its segment covered; the cut a marker
	// forced lands exactly on the segment end, so the end is inclusive. One
	// microsecond of slack absorbs the rounding of storage-side durations.
	constexpr int64_t kSettleSlackUs = 1;

	// Positions and durations are converted from the source timebase one value
	// at a time, so a plan can be missed by a microsecond. Missing it costs a
	// whole keyframe interval, and a millisecond is far below one frame, so it
	// can never bring a cut forward to the wrong frame.
	constexpr int64_t kPlanSlackUs = 1000;

	SegmentBoundaryPolicy::SegmentBoundaryPolicy(const Config &config)
		: _segment_duration_us(static_cast<int64_t>(config.segment_duration_ms) * 1000),
		  _chunk_duration_us(static_cast<int64_t>(config.chunk_duration_ms * 1000.0 + 0.5)),
		  _cue_out_cut_mode(config.cue_out_cut_mode),
		  _log_context(config.log_context),
		  _next_segment_number(config.initial_segment_number)
	{
		// A track that cuts only at a keyframe reaches its cadence in whole
		// keyframe intervals, so the shortest packageable break follows that
		// rounded cadence instead of the configured duration
		_cut_cadence_us = _segment_duration_us;
		if (config.keyframe_interval_ms > 0)
		{
			int64_t keyframe_interval_us = static_cast<int64_t>(config.keyframe_interval_ms * 1000.0 + 0.5);
			int64_t intervals = (_segment_duration_us + keyframe_interval_us - 1) / keyframe_interval_us;
			_cut_cadence_us = std::max(_cut_cadence_us, intervals * keyframe_interval_us);
		}
	}

	//--------------------------------------------------------------------------
	// Markers
	//--------------------------------------------------------------------------

	int64_t SegmentBoundaryPolicy::GetEventDurationMs(const std::shared_ptr<Marker> &marker)
	{
		if (marker->GetMarkerFormat() == cmn::BitstreamFormat::CUE && marker->GetCueEvent() != nullptr)
		{
			return marker->GetCueEvent()->GetDurationMsec();
		}
		if (marker->GetMarkerFormat() == cmn::BitstreamFormat::SCTE35 && marker->GetScte35Event() != nullptr)
		{
			return marker->GetScte35Event()->GetDurationMsec();
		}

		return -1;
	}

	int64_t SegmentBoundaryPolicy::EffectiveCutUs(const std::shared_ptr<Marker> &marker) const
	{
		int64_t timestamp_us = marker->GetTimestampMs() * 1000;

		// A provisional IN is the companion of its OUT: it returns exactly one
		// break length after the OUT's advertised time. An explicit IN states an
		// absolute position instead.
		if (marker->IsOutOfNetwork() == false && marker->IsProvisional() == true && _last_inserted_marker != nullptr)
		{
			auto parent_out = (_last_inserted_marker->IsOutOfNetwork() == true) ? _last_inserted_marker : _last_inserted_marker->GetParent();
			if (parent_out != nullptr)
			{
				int64_t break_duration_ms = GetEventDurationMs(parent_out);
				if (break_duration_ms > 0)
				{
					timestamp_us = (parent_out->GetTimestampMs() + break_duration_ms) * 1000;
				}
			}
		}

		int64_t newest_us = _newest_sample_end_us.load(std::memory_order_relaxed);

		if (newest_us < 0 || timestamp_us >= newest_us)
		{
			return timestamp_us;
		}

		// Late, but the next cut can still carry it
		if (newest_us - timestamp_us <= _cut_cadence_us)
		{
			return newest_us;
		}

		return -1;
	}

	std::tuple<bool, ov::String> SegmentBoundaryPolicy::CanInsertMarker(const std::shared_ptr<Marker> &marker) const
	{
		std::shared_lock<std::shared_mutex> lock(_markers_guard);

		return CanInsertMarkerUnlocked(marker);
	}

	std::tuple<bool, ov::String> SegmentBoundaryPolicy::CanInsertMarkerUnlocked(const std::shared_ptr<Marker> &marker) const
	{
		auto out_of_network = marker->IsOutOfNetwork();
		if (out_of_network.has_value() == false)
		{
			return {false, ov::String::FormatString("Failed to get out of network value of the marker : %s", marker->GetTag().CStr())};
		}
		bool is_out = out_of_network.value();

		// A break must span at least one cadence: the return lands at the first
		// cuttable position after the break, so a shorter one could land the
		// return on the very cut that opened it, a zero-length break
		const int64_t required_gap_us = _cut_cadence_us;
		if (is_out == true)
		{
			int64_t duration_ms = GetEventDurationMs(marker);
			if (duration_ms < 0)
			{
				return {false, ov::String::FormatString("Failed to get the event from the marker : %s", marker->GetTag().CStr())};
			}

			if (duration_ms * 1000 < required_gap_us)
			{
				return {false, ov::String::FormatString("Duration of the marker must be at least %f ms : %s", static_cast<double>(required_gap_us) / 1000.0, marker->GetTag().CStr())};
			}
		}

		int64_t effective_us = EffectiveCutUs(marker);
		if (effective_us < 0)
		{
			return {false, ov::String::FormatString("The position of the marker has already passed by more than a segment : %s (%" PRId64 " ms)", marker->GetTag().CStr(), marker->GetTimestampMs())};
		}

		if (_last_inserted_marker == nullptr)
		{
			if (is_out == false)
			{
				return {false, "First marker must be OUT marker"};
			}

			return {true, ""};
		}

		auto last_out_of_network = _last_inserted_marker->IsOutOfNetwork();
		if (last_out_of_network.has_value() == false)
		{
			return {false, "Failed to get out of network value of the last inserted marker"};
		}
		bool last_is_out = last_out_of_network.value();
		// The spacing rules are about where the cuts land, not the advertised times
		int64_t last_us = _last_inserted_cut_us;

		if (is_out == true)
		{
			if (last_is_out == true)
			{
				return {false, "OUT marker cannot be inserted after OUT marker"};
			}
			if (last_us > effective_us)
			{
				return {false, "OUT marker cannot be inserted before IN marker"};
			}
			if (effective_us - last_us < required_gap_us)
			{
				return {false, ov::String::FormatString("OUT marker must be inserted with the gap of %f ms", static_cast<double>(required_gap_us) / 1000.0)};
			}
		}
		else
		{
			if (last_is_out == false)
			{
				bool last_still_pending = (_pending_markers.find(_last_inserted_cut_us) != _pending_markers.end());

				if (last_still_pending == true && last_us >= effective_us)
				{
					// An earlier IN cancels the pending one; the same timestamp is a
					// duplicate of the same return point and replaces it harmlessly.
					// Once it has been emitted there is no open break left to modify.
				}
				else if (_last_inserted_marker->IsProvisional() == true && last_still_pending == true && marker->IsProvisional() == false)
				{
					// Only an explicit IN may move a pending provisional return point
					// later; a provisional one is the companion of an OUT and must not
					// extend the open break on its own
				}
				else
				{
					return {false, "IN marker can only be modified while it is pending, with an equal or earlier timestamp"};
				}
			}
			else
			{
				if (last_us > effective_us)
				{
					return {false, "IN marker cannot be inserted before OUT marker"};
				}
				if (effective_us - last_us < required_gap_us)
				{
					return {false, ov::String::FormatString("IN marker must be inserted with the gap of %f ms", static_cast<double>(required_gap_us) / 1000.0)};
				}
			}
		}

		return {true, ""};
	}

	std::optional<SegmentBoundaryPolicy::PreparedMarker> SegmentBoundaryPolicy::PrepareMarker(const std::shared_ptr<Marker> &marker) const
	{
		std::shared_lock<std::shared_mutex> lock(_markers_guard);

		return PrepareMarkerUnlocked(marker);
	}

	std::optional<SegmentBoundaryPolicy::PreparedMarker> SegmentBoundaryPolicy::PrepareMarkerUnlocked(const std::shared_ptr<Marker> &marker) const
	{
		auto [can_insert, message] = CanInsertMarkerUnlocked(marker);
		if (can_insert == false)
		{
			logte("%s - Failed to insert the marker : %s", _log_context.CStr(), message.CStr());
			return std::nullopt;
		}

		PreparedMarker prepared;
		prepared.marker = marker;

		// The position the marker cuts at: a provisional IN follows its OUT, a
		// late position moves onto the nearest one that can still cut; the
		// advertised time stays as received
		prepared.cut_us = EffectiveCutUs(marker);
		if (prepared.cut_us < 0)
		{
			logte("%s - The position of the marker has already passed by more than a segment : %s (%" PRId64 " ms)", _log_context.CStr(), marker->GetTag().CStr(), marker->GetTimestampMs());
			return std::nullopt;
		}

		if (_last_inserted_marker != nullptr && marker->IsOutOfNetwork() == false)
		{
			if (_last_inserted_marker->IsOutOfNetwork() == false)
			{
				// This IN replaces the pending one (duplicate, earlier return
				// point, or a provisional one being confirmed or moved) and keeps
				// its break
				prepared.replaces_cut_us = _last_inserted_cut_us;
				prepared.parent = _last_inserted_marker->GetParent();
			}
			else
			{
				// The return point of the break the last OUT opened
				prepared.parent = _last_inserted_marker;
			}
		}

		return prepared;
	}

	void SegmentBoundaryPolicy::CommitMarker(const PreparedMarker &prepared)
	{
		if (prepared.marker == nullptr || prepared.cut_us < 0)
		{
			return;
		}

		if (prepared.cut_us != prepared.marker->GetTimestampMs() * 1000)
		{
			logti("%s - Marker (%s) at %" PRId64 " ms cuts at %.3f ms; the advertised time is kept",
				  _log_context.CStr(), prepared.marker->GetTag().CStr(), prepared.marker->GetTimestampMs(), static_cast<double>(prepared.cut_us) / 1000.0);
		}

		std::lock_guard<std::shared_mutex> lock(_markers_guard);

		CommitMarkerUnlocked(prepared);
	}

	void SegmentBoundaryPolicy::CommitMarkerUnlocked(const PreparedMarker &prepared)
	{
		if (prepared.parent != nullptr)
		{
			prepared.marker->SetParent(prepared.parent);
		}

		if (prepared.replaces_cut_us >= 0)
		{
			_pending_markers.erase(prepared.replaces_cut_us);
		}

		_pending_markers[prepared.cut_us] = prepared.marker;
		_last_inserted_marker = prepared.marker;
		_last_inserted_cut_us = prepared.cut_us;
		_pending_marker_count.store(_pending_markers.size(), std::memory_order_relaxed);
	}

	bool SegmentBoundaryPolicy::InsertMarker(const std::shared_ptr<Marker> &marker)
	{
		// Deciding and applying under one exclusive lock: two inserts racing here
		// would both validate against the same state and then both apply, leaving
		// the OUT-then-OUT chain the validation exists to make impossible
		std::lock_guard<std::shared_mutex> lock(_markers_guard);

		auto prepared = PrepareMarkerUnlocked(marker);
		if (prepared.has_value() == false)
		{
			return false;
		}

		CommitMarkerUnlocked(*prepared);

		return true;
	}

	void SegmentBoundaryPolicy::RequestDiscontinuity(int64_t boundary_timestamp_us)
	{
		// Tracks changing together propagate to each other; a boundary within a
		// segment length of the last handled one is the same event
		int64_t last_discontinuity_us = _last_discontinuity_us.load(std::memory_order_relaxed);
		if (last_discontinuity_us >= 0)
		{
			int64_t delta_us = boundary_timestamp_us - last_discontinuity_us;
			if (delta_us < 0)
			{
				delta_us = -delta_us;
			}
			if (delta_us <= _segment_duration_us)
			{
				return;
			}
		}

		int64_t pending_us = _pending_discontinuity_cut_us.load(std::memory_order_relaxed);
		while (pending_us < 0 || boundary_timestamp_us < pending_us)
		{
			if (_pending_discontinuity_cut_us.compare_exchange_weak(pending_us, boundary_timestamp_us, std::memory_order_relaxed) == true)
			{
				break;
			}
		}
	}

	std::pair<int64_t, std::shared_ptr<Marker>> SegmentBoundaryPolicy::GetFirstPendingCut() const
	{
		if (_pending_marker_count.load(std::memory_order_relaxed) == 0)
		{
			return {-1, nullptr};
		}

		std::shared_lock<std::shared_mutex> lock(_markers_guard);

		if (_pending_markers.empty() == true)
		{
			return {-1, nullptr};
		}

		auto it = _pending_markers.begin();
		return {it->first, it->second};
	}

	SegmentBoundaryPolicy::SettledMarkers SegmentBoundaryPolicy::PopMarkersForSettlement(int64_t start_us, int64_t end_us)
	{
		std::lock_guard<std::shared_mutex> lock(_markers_guard);

		SettledMarkers settled;

		// The breaks discarded below; their return points go with them
		std::vector<std::shared_ptr<Marker>> discarded_breaks;

		for (auto it = _pending_markers.begin(); it != _pending_markers.end();)
		{
			if (it->first > end_us + kSettleSlackUs)
			{
				break;
			}

			// A break discarded earlier in this very pass takes its return point
			// with it, even one this segment would otherwise settle; the map order
			// guarantees the break was visited first (its cut is never later)
			auto parent = it->second->GetParent();
			if (parent != nullptr && std::find(discarded_breaks.begin(), discarded_breaks.end(), parent) != discarded_breaks.end())
			{
				logtw("%s - Dropped the return point (%s, %" PRId64 " ms) of the discarded break",
					  _log_context.CStr(), it->second->GetTag().CStr(), it->second->GetTimestampMs());

				if (it->second == _last_inserted_marker)
				{
					_last_inserted_marker = nullptr;
					_last_inserted_cut_us = -1;
				}

				it = _pending_markers.erase(it);
				continue;
			}

			if (it->first >= start_us)
			{
				// This segment covers the position, so the marker cut it
				settled.markers.push_back(it->second);
				settled.cut_this_segment = true;
			}
			else if (start_us - it->first <= _segment_duration_us)
			{
				// The media never reached the position (the keyframe wait after a
				// track change leaves a hole). The nearest boundary that can still
				// carry the marker is this one, and losing the tag is worse than
				// carrying it late. It cut nothing, so the pacing must not
				// discount this segment.
				logti("%s - Marker (%s, %" PRId64 " ms) rides segment ending at %.3f ms; the media never reached its position",
					  _log_context.CStr(), it->second->GetTag().CStr(), it->second->GetTimestampMs(), end_us / 1000.0);
				settled.markers.push_back(it->second);
			}
			else
			{
				// Further behind than the insertion clamp would ever accept: the
				// advertised time no longer describes where it would land, and a
				// leftover would block every later marker
				logtw("%s - Discarded the marker (%s, %" PRId64 " ms) because its position is more than a segment behind",
					  _log_context.CStr(), it->second->GetTag().CStr(), it->second->GetTimestampMs());

				if (it->second->IsOutOfNetwork() == true)
				{
					discarded_breaks.push_back(it->second);
				}

				// The chain must not keep validating against a marker that never cut
				if (it->second == _last_inserted_marker)
				{
					_last_inserted_marker = nullptr;
					_last_inserted_cut_us = -1;
				}
			}

			it = _pending_markers.erase(it);
		}

		// A break that never rendered has no return to render either: a lone IN
		// would announce a return from a break the playlist never opened
		for (auto it = _pending_markers.begin(); (discarded_breaks.empty() == false) && (it != _pending_markers.end());)
		{
			auto parent = it->second->GetParent();
			if (parent != nullptr && std::find(discarded_breaks.begin(), discarded_breaks.end(), parent) != discarded_breaks.end())
			{
				logtw("%s - Dropped the return point (%s, %" PRId64 " ms) of the discarded break",
					  _log_context.CStr(), it->second->GetTag().CStr(), it->second->GetTimestampMs());

				if (it->second == _last_inserted_marker)
				{
					_last_inserted_marker = nullptr;
					_last_inserted_cut_us = -1;
				}

				it = _pending_markers.erase(it);
				continue;
			}

			++it;
		}

		_pending_marker_count.store(_pending_markers.size(), std::memory_order_relaxed);

		return settled;
	}

	//--------------------------------------------------------------------------
	// Settlement
	//--------------------------------------------------------------------------

	CompletionResult SegmentBoundaryPolicy::OnSegmentCompleted(const CompletedSegment &completed)
	{
		return Settle(completed, false);
	}

	CompletionResult SegmentBoundaryPolicy::OnDiscontinuity(const CompletedSegment &completed)
	{
		return Settle(completed, true);
	}

	CompletionResult SegmentBoundaryPolicy::Settle(const CompletedSegment &completed, bool discontinuity)
	{
		int64_t segment_end_us = completed.start_timestamp_us + completed.duration_us;

		// A discontinuity with nothing in progress has no range to settle
		SettledMarkers settled_markers;
		if (completed.number >= 0)
		{
			settled_markers = PopMarkersForSettlement(completed.start_timestamp_us, segment_end_us);
		}
		const auto &covered_markers = settled_markers.markers;

		CompletedSegment settled = completed;
		// Only a marker that cut this segment gained it a boundary; one that
		// merely rides it must not shorten the next target
		settled.has_marker = settled_markers.cut_this_segment;

		if (discontinuity == true)
		{
			// The break landed; a propagated cut near it is the same event
			_pending_discontinuity_cut_us.store(-1, std::memory_order_relaxed);
			_last_discontinuity_us.store((completed.number >= 0) ? segment_end_us : _newest_sample_end_us.load(std::memory_order_relaxed), std::memory_order_relaxed);
		}

		auto completion_result = (discontinuity == true) ? DoOnDiscontinuity(settled, covered_markers)
														 : DoOnSegmentCompleted(settled, covered_markers);

		if (covered_markers.empty() == false)
		{
			completion_result.markers.insert(completion_result.markers.begin(), covered_markers.begin(), covered_markers.end());
		}

		AdvanceNumbering(completed);

		return completion_result;
	}

	//--------------------------------------------------------------------------
	// Emission
	//--------------------------------------------------------------------------

	ChunkPlan SegmentBoundaryPolicy::GetChunkPlan(const Samples &buffered,
												  const SampleTiming &next_frame,
												  int64_t segment_start_us,
												  int64_t last_segment_duration_us)
	{
		ChunkPlan decision;

		// The clamp floor for late markers. Written here only, so a guarded
		// store is enough; the atomic is for the marker path reading it.
		int64_t next_frame_end_us = next_frame.dts_us + next_frame.duration_us;
		if (next_frame_end_us > _newest_sample_end_us.load(std::memory_order_relaxed))
		{
			_newest_sample_end_us.store(next_frame_end_us, std::memory_order_relaxed);
		}

		if (_first_sample_end_us.load(std::memory_order_relaxed) < 0)
		{
			_first_sample_end_us.store(next_frame_end_us, std::memory_order_relaxed);
		}

		const auto &sample_list = buffered.GetList();

		// A propagated timeline break waits for a cuttable frame like every
		// other reason to close
		int64_t pending_discontinuity_cut_us = _pending_discontinuity_cut_us.load(std::memory_order_relaxed);
		bool break_reached = (pending_discontinuity_cut_us >= 0 && next_frame.dts_us >= pending_discontinuity_cut_us);

		if (sample_list.empty() == true)
		{
			// Nothing buffered, so the stored part of the segment closes alone
			if (break_reached == true && next_frame.independent == true)
			{
				decision.completes_segment = true;
				decision.discontinuity = true;
			}

			return decision;
		}

		int64_t buffered_duration_us = buffered.GetTotalTimingDurationUs();
		int64_t next_buffered_duration_us = buffered_duration_us + next_frame.duration_us;

		int64_t buffer_start_us = sample_list.front().timing.dts_us;
		int64_t buffer_end_us = sample_list.back().timing.dts_us + sample_list.back().timing.duration_us;

		auto boundary = GetSegmentBoundary(segment_start_us);
		int64_t target_segment_duration_us = boundary.end_us - segment_start_us;

		// The nearest pending marker demands a cut at its position: an OUT in
		// immediate mode right there even off a non-keyframe, everything else at
		// the first cuttable frame at or after it
		bool marker_cut_now = false;
		bool marker_close_asap = false;
		int64_t marker_cut_us = -1;
		auto [pending_cut_us, pending_marker] = GetFirstPendingCut();
		// A position at or before this segment's start needs no cut of its own:
		// the segment already begins at or after it, so cutting would only leave
		// a fragment behind. The settlement attaches the marker to it.
		if (pending_marker != nullptr && pending_cut_us > segment_start_us)
		{
			marker_cut_us = pending_cut_us;
			bool is_out = (pending_marker->IsOutOfNetwork() == true);

			if (is_out == true && _cue_out_cut_mode == LLHlsCueOutCutMode::Immediate)
			{
				if (next_frame.dts_us >= marker_cut_us || buffer_end_us > marker_cut_us)
				{
					marker_cut_now = true;
				}
			}
			else
			{
				// The frame that reaches the position leads the NEXT segment; a
				// position inside a frame's span waits for the following frame,
				// so the closing segment always covers it
				if (next_frame.dts_us >= marker_cut_us)
				{
					marker_close_asap = true;
				}
			}
		}

		// Where the segment would end if it closed at the arriving frame. Sample
		// durations are rounded to the source timebase, so their sum can fall a
		// millisecond short of the plan while the position says otherwise; the
		// next chance is a whole keyframe interval away, so the position decides.
		int64_t end_if_closed_here_us = next_frame.dts_us - segment_start_us;

		// Whether the emitted chunk may close the segment: the plan is reached,
		// or a marker or a timeline break demands the close
		bool can_be_last_chunk = false;
		if ((buffered_duration_us + last_segment_duration_us >= target_segment_duration_us - kPlanSlackUs) ||
			(end_if_closed_here_us >= target_segment_duration_us - kPlanSlackUs) ||
			(marker_close_asap == true && next_frame.independent == true) ||
			(break_reached == true && next_frame.independent == true) ||
			(marker_cut_now == true))
		{
			can_be_last_chunk = true;
		}

		// Ask the gate about the chunk this pass would actually emit, never the
		// whole backlog: a track feeding ahead of the boundaries would otherwise
		// move the question past them on every new sample and stay held until
		// the next boundary released it in one burst
		int64_t emit_end_us = std::min({buffer_end_us, boundary.end_us, buffer_start_us + _chunk_duration_us});

		if (CanEmitChunk(emit_end_us) == false)
		{
			return decision;
		}

		// A part must stay within the part target and reach at least 85% of it,
		// except an independent or segment-final part (rfc8216bis 4.4.4.9)
		if (((buffered_duration_us >= _chunk_duration_us) ||
			 (marker_cut_now == true) ||
			 (can_be_last_chunk == true && next_frame.independent == true) ||
			 ((next_buffered_duration_us > _chunk_duration_us) && (buffered_duration_us >= (_chunk_duration_us * 85) / 100))) == false)
		{
			return decision;
		}

		size_t emit_count = sample_list.size();
		// The duration of the emit_count prefix, kept in step with every trim
		int64_t emit_duration_us = buffered_duration_us;
		bool completed_by_split = false;
		bool no_marker = (marker_cut_now == false && marker_close_asap == false);

		// An immediate marker cut ends the segment exactly at the marker;
		// samples past it open the break content's replacement point
		if (marker_cut_now == true)
		{
			size_t samples_before_marker = 0;
			int64_t duration_before_marker_us = 0;
			while (samples_before_marker < sample_list.size() && sample_list[samples_before_marker].timing.dts_us < marker_cut_us)
			{
				duration_before_marker_us += sample_list[samples_before_marker].timing.duration_us;
				samples_before_marker++;
			}

			// Also when nothing precedes the marker: the segment then ends on
			// what is already stored, and the whole buffer opens the next one.
			// Letting it out here would write content from after the break into
			// the segment the break is supposed to end.
			emit_count = samples_before_marker;
			emit_duration_us = duration_before_marker_us;
		}

		// Samples past an exact boundary belong to the next segment, so the
		// boundary lands exactly where the plan said. Never engages while cuts
		// fire at the crossing frame (nothing accumulates past the plan then).
		if (can_be_last_chunk == true && no_marker == true && boundary.exact == true &&
			buffer_end_us > boundary.end_us)
		{
			size_t samples_before_boundary = 0;
			int64_t duration_before_boundary_us = 0;
			while (samples_before_boundary < sample_list.size() && sample_list[samples_before_boundary].timing.dts_us < boundary.end_us)
			{
				duration_before_boundary_us += sample_list[samples_before_boundary].timing.duration_us;
				samples_before_boundary++;
			}

			if (samples_before_boundary > 0 && samples_before_boundary < sample_list.size())
			{
				emit_count = samples_before_boundary;
				emit_duration_us = duration_before_boundary_us;

				// The boundary frame itself leads the samples left behind; when
				// it can open the next segment, the segment completes right here
				// instead of waiting for a later keyframe
				completed_by_split = sample_list[samples_before_boundary].timing.independent;
				if (completed_by_split == false)
				{
					logtw("%s - The boundary at %.3f ms is not a keyframe on this track; the segment closes at the next keyframe instead",
						  _log_context.CStr(), static_cast<double>(boundary.end_us) / 1000.0);
				}
			}
		}

		// A backlog longer than the chunk target may only go out one chunk-sized
		// slice per pass (a part longer than PART-TARGET is a spec violation and
		// Apple players reject the rendition)
		if (boundary.exact == true && no_marker == true)
		{
			if (emit_duration_us > _chunk_duration_us)
			{
				int64_t accumulated_us = 0;
				size_t slice_count = 0;
				while (slice_count < emit_count && accumulated_us + sample_list[slice_count].timing.duration_us <= _chunk_duration_us)
				{
					accumulated_us += sample_list[slice_count].timing.duration_us;
					slice_count++;
				}

				if (slice_count > 0 && slice_count < emit_count)
				{
					emit_count = slice_count;

					// The samples left behind still hold the segment tail, so
					// this pass may not complete the segment
					completed_by_split = false;
					can_be_last_chunk = false;
				}
			}
		}

		// A part cut mid-reorder covers different presentation time than its
		// declared duration says, and players that compare the two (Shaka)
		// report the mismatch. So the cut moves back to the last position where
		// the reordering had resolved; the samples left behind ride the next
		// part. An immediate marker cut is exempt: it cuts exactly where asked.
		bool reorder_resolved = (next_frame.pts_us > buffered.GetHighestPtsUs());

		if (reorder_resolved == false && no_marker == true &&
			completed_by_split == false && emit_count > 1)
		{
			// The lowest presentation timestamp from each position on, to test a
			// candidate cut against everything it would keep. Spans the whole
			// buffer, not the emit window: the samples past that window stay
			// behind as well, and a cut that displays after one of them is the
			// overlap this block exists to prevent.
			const size_t buffered_count = sample_list.size();
			std::vector<int64_t> lowest_kept_pts_us(buffered_count, 0);
			int64_t lowest_us = 0;
			for (size_t index = buffered_count; index-- > 0;)
			{
				lowest_us = (index + 1 == buffered_count) ? sample_list[index].timing.pts_us : std::min(lowest_us, sample_list[index].timing.pts_us);
				lowest_kept_pts_us[index] = lowest_us;
			}

			size_t resolved_count = 0;
			int64_t highest_emitted_pts_us = 0;
			for (size_t index = 0; index + 1 < emit_count; index++)
			{
				highest_emitted_pts_us = (index == 0) ? sample_list[index].timing.pts_us : std::max(highest_emitted_pts_us, sample_list[index].timing.pts_us);

				if (highest_emitted_pts_us < lowest_kept_pts_us[index + 1])
				{
					resolved_count = index + 1;
				}
			}

			// However short the resolved prefix is, it goes out on its own: a
			// part that covers a different stretch of presentation time than it
			// declares is worse than a short one
			if (resolved_count > 0)
			{
				emit_count = resolved_count;
				can_be_last_chunk = false;
			}
		}

		decision.emit_count = emit_count;
		decision.completes_segment = (can_be_last_chunk == true && next_frame.independent == true) ||
									 (marker_cut_now == true) || (completed_by_split == true);

		// Last resort: no cuttable position appeared for twice the cadence, so
		// the segment closes here rather than growing without bound
		if (decision.completes_segment == false)
		{
			int64_t emitted_duration_us = 0;
			for (size_t index = 0; index < emit_count; index++)
			{
				emitted_duration_us += sample_list[index].timing.duration_us;
			}

			if (last_segment_duration_us + emitted_duration_us > _segment_duration_us * 2)
			{
				logte("%s - No cuttable position (keyframe or boundary) appeared for %.3f ms, twice the target %.3f ms; the segment is closed here and may not play normally",
					  _log_context.CStr(), (last_segment_duration_us + emitted_duration_us) / 1000.0, _segment_duration_us / 1000.0);
				decision.completes_segment = true;
			}
		}

		// A segment that runs well past its plan is worth reporting: the cut
		// could not land where the cadence wanted it
		if (decision.completes_segment == true && target_segment_duration_us > 0)
		{
			int64_t closed_at_us = last_segment_duration_us;
			for (size_t index = 0; index < emit_count; index++)
			{
				closed_at_us += sample_list[index].timing.duration_us;
			}

			if (closed_at_us > target_segment_duration_us + next_frame.duration_us)
			{
				logtw("%s - Segment closed at %.3f ms, past its %.3f ms plan (buffered %.3f, stored %.3f, frame dts %.3f, start %.3f)",
					  _log_context.CStr(), closed_at_us / 1000.0, target_segment_duration_us / 1000.0,
					  buffered_duration_us / 1000.0, last_segment_duration_us / 1000.0,
					  next_frame.dts_us / 1000.0, segment_start_us / 1000.0);
			}
		}

		// The break is stamped where it actually happened, on the first close at
		// or after its position. Riding an earlier close instead would consume
		// the break on the tracks that received it while the track that changed
		// still closes at its own position, leaving that rendition one segment
		// ahead of the others.
		decision.discontinuity = break_reached && decision.completes_segment;

		return decision;
	}
}  // namespace bmff
