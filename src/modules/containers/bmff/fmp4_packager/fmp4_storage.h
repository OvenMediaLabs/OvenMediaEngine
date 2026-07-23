//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2022 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include "fmp4_structure.h"
#include <base/common_types.h>
#include <base/info/media_track.h>
#include <base/modules/container/segment_storage.h>
#include <base/modules/marker/marker_box.h>

namespace bmff
{
	class FMp4StorageObserver : public ov::EnableSharedFromThis<FMp4StorageObserver>
	{
	public:
		virtual void OnFMp4StorageInitialized(const int32_t &track_id) = 0;
		virtual void OnMediaSegmentCreated(const int32_t &track_id, const uint32_t &segment_number) = 0;
		virtual void OnMediaChunkUpdated(const int32_t &track_id, const uint32_t &segment_number, const uint32_t &chunk_number, bool last_chunk) = 0;
		virtual void OnMediaSegmentDeleted(const int32_t &track_id, const uint32_t &segment_number) = 0;
		// A segment was force-completed without a new chunk (track change boundary)
		virtual void OnMediaSegmentCompleted(const int32_t &track_id, const uint32_t &segment_number) = 0;
	};

	class FMP4Storage : public base::modules::SegmentStorage
	{
	public:
		struct Config
		{
			uint32_t max_segments = 10;
			uint64_t segment_duration_ms = 6000;
			bool dvr_enabled = false;
			ov::String dvr_storage_path;
			uint64_t dvr_duration_sec = 0;
			bool server_time_based_segment_numbering = false;
		};

		FMP4Storage(const std::shared_ptr<FMp4StorageObserver> &observer, const std::shared_ptr<const MediaTrack> &track, const Config &config, const ov::String &stream_tag);

		virtual ~FMP4Storage();

		std::shared_ptr<ov::Data> GetInitializationSection() const override;
		std::shared_ptr<ov::Data> GetInitializationSection(uint32_t track_version) const;
		// Snapshot of all retained sections, keyed by track version
		std::map<uint32_t, std::shared_ptr<ov::Data>> GetInitializationSections() const;
		std::shared_ptr<base::modules::Segment> GetSegment(int64_t segment_number) const override;
		std::shared_ptr<base::modules::Segment> GetLastSegment() const override;
		std::shared_ptr<base::modules::PartialSegment> GetPartialSegment(int64_t segment_number, int64_t partial_number) const override;
		uint64_t GetSegmentCount() const override;
		int64_t GetLastSegmentNumber() const override;
		std::tuple<int64_t, int64_t> GetLastPartialSegmentNumber() const override;
		
		bool StoreInitializationSection(const std::shared_ptr<ov::Data> &section);
		bool AppendMediaChunk(const std::shared_ptr<ov::Data> &chunk, int64_t start_timestamp, double duration_ms, bool independent, bool last_chunk, const std::vector<std::shared_ptr<Marker>> &markers = {});

		// Switch to a new version of the track at a runtime configuration change.
		// Completes the in-progress segment and creates subsequent segments with the
		// new track. The completed segment number (-1 if none) is handed back so the
		// caller can publish it via NotifySegmentCompleted once the new
		// initialization section exists.
		bool UpdateTrack(const std::shared_ptr<const MediaTrack> &track, int64_t &completed_segment_number);

		// Notify the observer of a segment completed without a new chunk
		void NotifySegmentCompleted(int64_t segment_number);

		// Complete the in-progress segment and start a new discontinuity domain
		// without a configuration change (another track of the stream changed)
		void CutSegmentForDiscontinuity();

		uint64_t GetMaxPartialDurationMs() const override;
		uint64_t GetMinPartialDurationMs() const override;

		ov::String GetContainerExtension() const override { return "m4s"; }

		double GetTargetSegmentDuration() const;

	private:
		std::shared_ptr<FMP4Segment> GetSegmentInternal(int64_t segment_number) const;
		std::shared_ptr<FMP4Segment> GetLastSegmentInternal() const;

		// Force the in-progress segment to complete at a track change boundary.
		// Returns the completed segment number, or -1 if there was nothing to complete.
		int64_t CompleteLastSegment();

		// Reset the segment duration pacing after a boundary cut
		void RealignSegmentDurationPacing();

		// Flag the pre-created empty segment as the start of a new discontinuity domain
		void MarkPendingSegmentDiscontinuity();

		void DropUnreferencedInitializationSections();
		

		// For DVR
		class DvrInfo
		{
		public:
			struct SegmentInfo
			{
				uint32_t segment_number = 0;
				double duration_ms = 0;
				size_t segment_size = 0;

				bool IsAvailable() const
				{
					return segment_size != 0;
				}
			}; 

			// Get total duration of all segments
			uint64_t GetTotalDurationMs() const
			{
				return _total_dvr_segment_duration_ms;
			}

			// Get total number of segments
			uint32_t GetTotalSegmentCount() const
			{
				std::shared_lock<std::shared_mutex> lock(_segments_lock);
				return _segments.size();
			}

			void AppendSegment(uint32_t segment_number, double duration_ms, size_t segment_size)
			{
				//lock
				std::lock_guard<std::shared_mutex> lock(_segments_lock);
				
				if (_segments.empty())
				{
					_first_segment_number = segment_number;
				}
				else
				{
					if (_segments.back().segment_number + 1 != segment_number)
					{
						logw("DVR", "Segment number is not continuous: %u -> %u", _segments.front().segment_number, segment_number);
					}
				}

				_segments.push_back({segment_number, duration_ms, segment_size});
				_total_dvr_segment_duration_ms += duration_ms;
			}

			// Pop oldest segment info
			SegmentInfo PopOldestSegmentInfo()
			{
				//lock
				std::lock_guard<std::shared_mutex> lock(_segments_lock);

				if(_segments.empty())
				{
					return {0, 0, 0};
				}

				auto segment_info = _segments.front();
				_segments.pop_front();
				_total_dvr_segment_duration_ms -= segment_info.duration_ms;

				// update first segment number
				_first_segment_number += 1;

				return segment_info;
			}

			// Get segment info
			SegmentInfo GetSegmentInfo(uint32_t segment_number) const
			{
				//lock
				std::shared_lock<std::shared_mutex> lock(_segments_lock);

				if(_segments.empty())
				{
					return {0, 0, 0};
				}

				// Check if the segment number is valid
				if (_first_segment_number > segment_number)
				{
					return {0, 0, 0};
				}

				auto index = segment_number - _first_segment_number;
				if (index >= _segments.size())
				{
					return {0, 0, 0};
				}

				return _segments[index];
			}

		private:
			std::deque<SegmentInfo> _segments;
			// segments lock
			mutable std::shared_mutex _segments_lock;
			uint32_t _first_segment_number = 0;
			double _total_dvr_segment_duration_ms = 0;
		};

		DvrInfo _dvr_info;

		ov::String GetDVRDirectory() const;
		ov::String GetSegmentFilePath(uint32_t segment_number) const;
		bool SaveMediaSegmentToFile(const std::shared_ptr<FMP4Segment> &segment);
		std::shared_ptr<FMP4Segment> LoadMediaSegmentFromFile(uint32_t segment_number) const;

		std::shared_ptr<FMP4Segment> CreateNextSegment();

		std::shared_ptr<const MediaTrack> GetTrack() const;

		Config	_config;

		// Swapped by UpdateTrack at a runtime configuration change, read via GetTrack
		std::shared_ptr<const MediaTrack> _track;

		// Initialization sections keyed by the track version that produced them.
		// A runtime track change stores a new entry so that segments of the old
		// version remain playable with their own section.
		std::map<uint32_t, std::shared_ptr<ov::Data>> _initialization_sections;
		// The first stored version, served for the version-less legacy URL
		uint32_t _initial_track_version = 0;
		mutable std::shared_mutex _initialization_sections_lock;

		// segment number : segment
		std::map<int64_t, std::shared_ptr<FMP4Segment>> _segments;
		mutable std::shared_mutex _segments_lock;

		int64_t _initial_segment_number = 0;
		[[maybe_unused]] int64_t _start_timestamp_delta = -1;

		double _max_chunk_duration_ms = 0;
		double _min_chunk_duration_ms = static_cast<double>(std::numeric_limits<uint64_t>::max());

		double _target_segment_duration_ms = 0;

		double _total_segment_duration_ms = 0;
		double _total_expected_duration_ms = 0;

		std::shared_ptr<FMp4StorageObserver> _observer;

		ov::String _stream_tag;

		// for making CUE-OUT-CONT
		[[maybe_unused]] bool _is_cue_out_on = false;
		[[maybe_unused]] uint32_t _cue_out_duration_msec = 0;
		[[maybe_unused]] uint32_t _cue_out_elapsed_msec = 0;
	};
}