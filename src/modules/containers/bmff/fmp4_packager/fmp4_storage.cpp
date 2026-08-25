//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2022 AirenSoft. All rights reserved.
//
//==============================================================================

#include <base/info/media_track.h>
#include <base/ovlibrary/files.h>

#include <base/modules/data_format/cue_event/cue_event.h>

#include "fmp4_storage.h"

#include <cmath>
#include "fmp4_private.h"

namespace bmff
{
	FMP4Storage::FMP4Storage(const std::shared_ptr<FMp4StorageObserver> &observer, const std::shared_ptr<const MediaTrack> &track, const FMP4Storage::Config &config, const ov::String &stream_tag, const std::shared_ptr<SegmentBoundaryPolicy> &boundary_policy)
	{
		_config = config;
		_track = track;
		_observer = observer;

		// Seed the content version from the track's initial version so the version-less
		// legacy URL keeps mapping to the first initialization section
		_content_version = track->GetVersion();

		_stream_tag = stream_tag;

		OV_ASSERT2(boundary_policy != nullptr);
		_boundary_policy = boundary_policy;
		if (_boundary_policy != nullptr)
		{
			_initial_segment_number = _boundary_policy->GetSegmentBoundary(std::nullopt).segment_number;
		}
	}

	FMP4Storage::~FMP4Storage()
	{
		if (_config.dvr_enabled == true)
		{
			// Delete all dvr directory and files
			auto dvr_path = GetDVRDirectory();

			logti("Try to delete directory for LLHLS DVR: %s", dvr_path.CStr());
			ov::DeleteDirectories(dvr_path);
			logti("Successfully deleted directory for LLHLS DVR: %s", dvr_path.CStr());
		}

		logtt("FMP4 Storage has been terminated successfully");
	}

	std::shared_ptr<const MediaTrack> FMP4Storage::GetTrack() const
	{
		return std::atomic_load(&_track);
	}

	std::shared_ptr<ov::Data> FMP4Storage::GetInitializationSection() const
	{
		// The version-less legacy URL always means the first section of the stream
		std::shared_lock<std::shared_mutex> lock(_initialization_sections_lock);
		auto it = _initialization_sections.find(_initial_track_version);
		if (it == _initialization_sections.end())
		{
			return nullptr;
		}

		return it->second;
	}

	std::shared_ptr<ov::Data> FMP4Storage::GetInitializationSection(uint32_t track_version) const
	{
		std::shared_lock<std::shared_mutex> lock(_initialization_sections_lock);
		auto it = _initialization_sections.find(track_version);
		if (it == _initialization_sections.end())
		{
			return nullptr;
		}

		return it->second;
	}

	std::map<uint32_t, std::shared_ptr<ov::Data>> FMP4Storage::GetInitializationSections() const
	{
		std::shared_lock<std::shared_mutex> lock(_initialization_sections_lock);
		return _initialization_sections;
	}

	std::shared_ptr<base::modules::Segment> FMP4Storage::GetSegment(int64_t segment_number) const
	{
		return GetSegmentInternal(static_cast<int64_t>(segment_number));
	}

	std::shared_ptr<FMP4Segment> FMP4Storage::GetSegmentInternal(int64_t segment_number) const
	{
		std::shared_lock<std::shared_mutex> lock(_segments_lock);
		
		if (_segments.empty())
		{
			return nullptr;
		}

		auto it = _segments.find(segment_number);
		if (it != _segments.end())
		{
			return it->second;
		}
		
		auto min_number = _segments.begin()->first;
		if (segment_number < min_number)
		{
			// If the segment is not in the list, try to load it from the file
			return LoadMediaSegmentFromFile(segment_number);
		}

		return nullptr;
	}

	std::shared_ptr<base::modules::Segment> FMP4Storage::GetLastSegment() const
	{
		return GetLastSegmentInternal();	
	}

	std::shared_ptr<FMP4Segment> FMP4Storage::GetLastSegmentInternal() const
	{
		std::shared_lock<std::shared_mutex> lock(_segments_lock);
		if (_segments.empty())
		{
			return nullptr;
		}

		// get last segment
		return _segments.rbegin()->second;
	}

	std::shared_ptr<base::modules::PartialSegment> FMP4Storage::GetPartialSegment(int64_t segment_number, int64_t partial_number) const
	{
		// Get Media Segement
		auto segment = GetSegmentInternal(segment_number);
		if (segment == nullptr)
		{
			return nullptr;
		}

		// last chunk number + 1 of completed segment is the first chunk of the next segment
		if (segment->IsCompleted() && segment->GetLastPartialNumber() + 1 == partial_number)
		{
			auto next_segment = GetSegmentInternal(segment_number + 1);
			if (next_segment == nullptr)
			{
				return nullptr;
			}

			// Never roll over across a discontinuity; the hinted name belongs to the
			// previous domain and its content would be appended without the new
			// initialization segment. The player re-syncs from the playlist on 404.
			if (next_segment->IsDiscontinuityPoint() == true ||
				next_segment->GetTrackVersion() != segment->GetTrackVersion())
			{
				return nullptr;
			}

			segment = next_segment;
			partial_number = 0;
		}

		// Get Media Chunk
		auto partial = segment->GetPartialSegment(partial_number);
		if (partial == nullptr)
		{
			return nullptr;
		}

		return partial;
	}

	uint64_t FMP4Storage::GetSegmentCount() const
	{
		std::shared_lock<std::shared_mutex> lock(_segments_lock);
		return _segments.size();
	}

	std::tuple<int64_t, int64_t> FMP4Storage::GetLastPartialSegmentNumber() const
	{
		auto last_segment = GetLastSegmentInternal();
		if (last_segment == nullptr)
		{
			return { -1, -1 };
		}

		return { last_segment->GetNumber(), last_segment->GetLastPartialNumber() };
	}

	int64_t FMP4Storage::GetLastSegmentNumber() const
	{
		auto last_segment = GetLastSegment();
		if (last_segment == nullptr)
		{
			// Nothing exists yet: one before where the numbering will start
			// (server-time numbering starts high, so a plain -1 would be wrong)
			return _initial_segment_number - 1;
		}

		return last_segment->GetNumber();
	}

	uint64_t FMP4Storage::GetMaxPartialDurationMs() const
	{
		return _max_chunk_duration_ms;
	}

	uint64_t FMP4Storage::GetMinPartialDurationMs() const
	{
		return _min_chunk_duration_ms;
	}

	bool FMP4Storage::StoreInitializationSection(const std::shared_ptr<ov::Data> &section)
	{
		auto track = GetTrack();
		auto content_version = GetContentVersion();

		{
			std::lock_guard<std::shared_mutex> lock(_initialization_sections_lock);

			if (_initialization_sections.empty())
			{
				_initial_track_version = content_version;
			}

			_initialization_sections[content_version] = section;
		}

		if (_observer != nullptr)
		{
			_observer->OnFMp4StorageInitialized(track->GetId());
		}
		return true;
	}

	bool FMP4Storage::UpdateTrack(const std::shared_ptr<const MediaTrack> &track, int64_t &completed_segment_number)
	{
		completed_segment_number = -1;

		auto old_track = GetTrack();
		if (track == nullptr || track->GetId() != old_track->GetId())
		{
			return false;
		}

		// A track configuration change starts a new content version, so its segments
		// and initialization section are addressed separately from the old track
		_content_version++;

		// Close the old content (the buffered samples were already flushed). The
		// completion is reported to the caller instead of the observer here, so that
		// it can be published after the new initialization section is stored.
		completed_segment_number = CompleteLastSegment(true);

		std::atomic_store(&_track, track);

		RebuildPendingSegmentOnCurrentTrack();

		MarkPendingSegmentDiscontinuity();

		return true;
	}

	void FMP4Storage::RebuildPendingSegmentOnCurrentTrack()
	{
		auto track = GetTrack();

		std::lock_guard<std::shared_mutex> segments_lock(_segments_lock);

		if (_segments.empty() == true)
		{
			return;
		}

		auto last_segment = _segments.rbegin()->second;

		// An empty segment pre-created at the last completion still carries the track
		// timebase and content version of that moment. Samples of the new content would
		// land in it and be served under the old version, so rebuild it.
		if (last_segment->IsCompleted() == false && last_segment->GetPartialCount() == 0)
		{
			auto new_segment = std::make_shared<FMP4Segment>(last_segment->GetNumber(), _config.segment_duration_ms, track->GetTimeBase().GetExpr());
			new_segment->SetTrackVersion(GetContentVersion());
			new_segment->SetCodecsParameter(track->GetCodecsParameter());
			_segments[last_segment->GetNumber()] = new_segment;
		}
	}

	void FMP4Storage::CutSegmentForDiscontinuity()
	{
		auto completed_segment_number = CompleteLastSegment(true);

		MarkPendingSegmentDiscontinuity();

		NotifySegmentCompleted(completed_segment_number);
	}

	void FMP4Storage::CutSegmentAtMarker()
	{
		// The segment ends on what is already stored: every buffered sample
		// belongs after the marker, so none of it may join this segment
		auto completed_segment_number = CompleteLastSegment(false);

		NotifySegmentCompleted(completed_segment_number);
	}

	void FMP4Storage::StartNewContentVersionForKeyRotation()
	{
		// A key change does not change the content, so no segment is cut and the duration
		// pacing is left alone. Only the version advances, so the segments encrypted with
		// the new key get their own initialization section and key tag.
		_content_version++;

		// The empty segment waiting for the new samples was stamped with the previous
		// version when it was pre-created
		RebuildPendingSegmentOnCurrentTrack();
	}

	void FMP4Storage::NotifySegmentCompleted(int64_t segment_number)
	{
		if (segment_number >= 0 && _observer != nullptr)
		{
			_observer->OnMediaSegmentCompleted(GetTrack()->GetId(), segment_number);
		}
	}

	void FMP4Storage::MarkPendingSegmentDiscontinuity()
	{
		std::lock_guard<std::shared_mutex> lock(_segments_lock);

		// Nothing was published yet; the first segment is not a discontinuity
		if (_segments.empty() == true)
		{
			return;
		}

		auto last_segment = _segments.rbegin()->second;
		if (last_segment->IsCompleted() == false && last_segment->GetPartialCount() == 0)
		{
			last_segment->SetDiscontinuityPoint();
		}
	}

	int64_t FMP4Storage::CompleteLastSegment(bool as_discontinuity)
	{
		if (_boundary_policy == nullptr)
		{
			return -1;
		}

		auto segment = GetLastSegmentInternal();
		if (segment == nullptr || segment->IsCompleted() == true || segment->GetPartialCount() == 0)
		{
			if (as_discontinuity == true)
			{
				// Nothing was in progress, but the timeline still broke here
				_boundary_policy->OnDiscontinuity({});
			}

			return -1;
		}

		double timescale = GetTrack()->GetTimeBase().GetTimescale();

		CompletedSegment completed_segment;
		completed_segment.number = segment->GetNumber();
		completed_segment.start_timestamp_us = (timescale > 0) ? TicksToUs(segment->GetStartTimestamp(), timescale) : 0;
		completed_segment.duration_us = std::llround(segment->GetDurationMs() * 1000.0);

		// The markers the closed segment covered still ride it. A marker cut is
		// an ordinary completion at the announced position, so only a timeline
		// break settles as a discontinuity.
		auto completion_result = (as_discontinuity == true) ? _boundary_policy->OnDiscontinuity(completed_segment)
															: _boundary_policy->OnSegmentCompleted(completed_segment);
		if (completion_result.markers.empty() == false)
		{
			segment->AddMarkers(completion_result.markers);
		}

		segment->SetCompleted();

		CreateNextSegment();

		return segment->GetNumber();
	}

	ov::String FMP4Storage::GetDVRDirectory() const
	{
		// Read via GetTrack, this path also runs on HTTP request threads
		return ov::String::FormatString("%s/%s/%d", _config.dvr_storage_path.CStr(), _stream_tag.CStr(), GetTrack()->GetId());
	}

	ov::String FMP4Storage::GetSegmentFilePath(uint32_t segment_number) const
	{
		return ov::String::FormatString("%s/%d.m4s", GetDVRDirectory().CStr(), segment_number);
	}

	bool FMP4Storage::SaveMediaSegmentToFile(const std::shared_ptr<FMP4Segment> &segment)
	{
		if (_config.dvr_enabled == false)
		{
			return false;
		}

		// Save to file
		auto file_path = GetSegmentFilePath(segment->GetNumber());
		auto dir = GetDVRDirectory();

		// Create directory
		if (ov::IsDirExist(dir) == false)
		{
			logti("Try to create directory for LLHLS DVR: %s", dir.CStr());
			if (ov::CreateDirectories(dir) == false)
			{
				logte("Could not create directory for DVR: %s", dir.CStr());
				return false;
			}
		}

		// Save to file
		if (ov::DumpToFile(file_path, segment->GetData()) == nullptr)
		{
			logte("Could not save segment to file: %s", file_path.CStr());
			return false;
		}

		_dvr_info.AppendSegment(segment->GetNumber(), segment->GetDurationMs(), segment->GetDataLength());

		// Delete old segments until the total duration is less than the maximum DVR duration
		while (_dvr_info.GetTotalDurationMs() > (_config.dvr_duration_sec * 1000.0))
		{
			auto segment_to_delete = _dvr_info.PopOldestSegmentInfo();
			if (segment_to_delete.IsAvailable() == false)
			{
				break;
			}

			auto file_path = GetSegmentFilePath(segment_to_delete.segment_number);
			if (std::remove(file_path) != 0)
			{
				logte("Could not delete DVR segment file: %s", file_path.CStr());
			}

			if (_observer != nullptr)
			{
				_observer->OnMediaSegmentDeleted(GetTrack()->GetId(), segment_to_delete.segment_number);
			}
		}

		return true;
	}

	std::shared_ptr<FMP4Segment> FMP4Storage::LoadMediaSegmentFromFile(uint32_t segment_number) const
	{
		if (_config.dvr_enabled == false)
		{
			return nullptr;
		}

		auto info = _dvr_info.GetSegmentInfo(segment_number);
		if (info.IsAvailable() == false)
		{
			logte("Could not find segment info: %u", segment_number);
			return nullptr;
		}

		auto file_path = GetSegmentFilePath(segment_number);

		auto data = ov::LoadFromFile(file_path);
		if (data == nullptr)
		{
			logte("Could not load segment from file: %s", file_path.CStr());
			return nullptr;
		}

		auto segment = std::make_shared<FMP4Segment>(segment_number, info.duration_ms, data);
		if (segment == nullptr)
		{
			logte("Could not create segment: %u", segment_number);
			return nullptr;
		}

		return segment;
	}

	std::shared_ptr<FMP4Segment> FMP4Storage::CreateNextSegment(std::optional<int64_t> first_chunk_start_timestamp_us)
	{
		if (_boundary_policy == nullptr)
		{
			return nullptr;
		}

		auto track = GetTrack();

		// The policy owns the numbering; the storage only guards its own segment
		// map. A number at or below one already published cannot be materialized
		// again, so it is reported as a defect and stepped past.
		auto plan = _boundary_policy->GetSegmentBoundary(first_chunk_start_timestamp_us);
		int64_t segment_number = plan.segment_number;

		bool has_segments = false;
		{
			std::shared_lock<std::shared_mutex> lock(_segments_lock);
			has_segments = (_segments.empty() == false);
		}
		if (has_segments == true && segment_number <= GetLastSegmentNumber())
		{
			logte("LLHLS stream (%s) / track (%u) - segment numbering conflicted (next %" PRId64 ", already published %" PRId64 "); renumbered to continue, renditions may pair segments wrongly in SSAI",
				  _stream_tag.CStr(), GetTrack()->GetId(), segment_number, GetLastSegmentNumber());
			segment_number = GetLastSegmentNumber() + 1;
		}

		// Create next segment
		auto segment = std::make_shared<FMP4Segment>(segment_number, _config.segment_duration_ms, track->GetTimeBase().GetExpr());
		segment->SetTrackVersion(GetContentVersion());
		segment->SetCodecsParameter(track->GetCodecsParameter());
		{
			std::lock_guard<std::shared_mutex> lock(_segments_lock);
			_segments.emplace(segment->GetNumber(), segment);

			// Delete old segments
			if (_segments.size() > _config.max_segments)
			{
				auto old_it = _segments.begin();
				std::advance(old_it, (_segments.size() - _config.max_segments) - 1);

				auto old_segment = old_it->second;

				// Since the chunklist is updated late, the player may request deleted segments in the meantime, so it actually deletes them a bit late.
				if (_segments.size() > _config.max_segments + 3)
				{
					_segments.erase(_segments.begin());
					DropUnreferencedInitializationSections();
				}

				// DVR
				if (_config.dvr_enabled)
				{
					SaveMediaSegmentToFile(old_segment);
				}
				else
				{
					if (_observer != nullptr)
					{
						_observer->OnMediaSegmentDeleted(track->GetId(), old_segment->GetNumber());
					}
				}
			}
		}

		if (_observer != nullptr)
		{
			_observer->OnMediaSegmentCreated(track->GetId(), segment->GetNumber());
		}

		return segment;
	}

	// Called with _segments_lock held
	void FMP4Storage::DropUnreferencedInitializationSections()
	{
		// DVR keeps old segments servable from files, so their sections must be kept too
		if (_config.dvr_enabled == true || _segments.empty() == true)
		{
			return;
		}

		// Versions are non-decreasing along segment numbers, so anything below the
		// oldest retained segment's version is unreferenced. The initial version is
		// kept for the version-less legacy URL.
		auto min_retained_version = _segments.begin()->second->GetTrackVersion();

		std::lock_guard<std::shared_mutex> lock(_initialization_sections_lock);

		for (auto it = _initialization_sections.begin(); it != _initialization_sections.end() && it->first < min_retained_version;)
		{
			if (it->first != _initial_track_version)
			{
				it = _initialization_sections.erase(it);
			}
			else
			{
				it++;
			}
		}
	}

	bool FMP4Storage::AppendMediaChunk(const std::shared_ptr<ov::Data> &chunk, int64_t start_timestamp, double duration_ms, bool independent, bool last_chunk, bool discontinuity)
	{
		if (_boundary_policy == nullptr)
		{
			return false;
		}

		double timescale = GetTrack()->GetTimeBase().GetTimescale();
		int64_t start_timestamp_us = (timescale > 0) ? TicksToUs(start_timestamp, timescale) : -1;

		auto segment = GetLastSegmentInternal();
		if (segment == nullptr || segment->IsCompleted() == true)
		{
			segment = CreateNextSegment(start_timestamp_us);
			if (segment == nullptr)
			{
				return false;
			}
		}

		if (segment->AppendPartialData(chunk, start_timestamp, duration_ms, independent) == false)
		{
			return false;
		}

		_boundary_policy->OnMediaChunk(start_timestamp_us, std::llround(duration_ms * 1000.0), independent, last_chunk);

		// Complete Segment if segment duration is over and new chunk data is independent(new segment should be started with independent chunk)
		if (last_chunk == true)
		{
			// The settlement before the next segment is pre-created: the policy's
			// numbering must see this completion before it names the next segment
			CompletedSegment completed_segment;
			completed_segment.number = segment->GetNumber();
			completed_segment.start_timestamp_us = (timescale > 0) ? TicksToUs(segment->GetStartTimestamp(), timescale) : 0;
			completed_segment.duration_us = std::llround(segment->GetDurationMs() * 1000.0);
			auto completion_result = (discontinuity == true) ? _boundary_policy->OnDiscontinuity(completed_segment)
															 : _boundary_policy->OnSegmentCompleted(completed_segment);

			// Markers realized on boundaries decided elsewhere, in addition to
			// the ones the packager consumed from its own sample range
			segment->AddMarkers(completion_result.markers);

			segment->SetCompleted();

			CreateNextSegment();

			if (discontinuity == true)
			{
				// The chunklist renders the break before the segment that follows it
				MarkPendingSegmentDiscontinuity();
			}

			logtt("Segment[%" PRId64 "] is created : track(%u), duration(%f) chunks(%lu)", segment->GetNumber(), GetTrack()->GetId(),segment->GetDurationMs(), segment->GetPartialCount());

			if (segment->HasMarker() == true)
			{
				logtd("LLHLS stream (%s) / track (%u) - segment[%" PRId64 "] has markers %s", _stream_tag.CStr(), GetTrack()->GetId(), segment->GetNumber(), segment->GetMarkers().back()->GetTag().CStr());
			}
		}

		_max_chunk_duration_ms = std::max(_max_chunk_duration_ms, duration_ms);
		_min_chunk_duration_ms = std::min(_min_chunk_duration_ms, duration_ms);

		// Notify observer
		if (_observer != nullptr)
		{
			bool last_chunk = segment->IsCompleted() == true;
			_observer->OnMediaChunkUpdated(GetTrack()->GetId(), segment->GetNumber(), segment->GetLastPartialNumber(), last_chunk);
		}

		return true;
	}
} // namespace bmff