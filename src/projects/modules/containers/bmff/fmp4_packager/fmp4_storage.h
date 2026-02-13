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
		std::shared_ptr<base::modules::Segment> GetSegment(int64_t segment_number) const override;
		std::shared_ptr<base::modules::Segment> GetLastSegment() const override;
		std::shared_ptr<base::modules::PartialSegment> GetPartialSegment(int64_t segment_number, int64_t partial_number) const override;
		uint64_t GetSegmentCount() const override;
		int64_t GetLastSegmentNumber() const override;
		std::tuple<int64_t, int64_t> GetLastPartialSegmentNumber() const override;
		
		bool StoreInitializationSection(const std::shared_ptr<ov::Data> &section);
		bool AppendMediaChunk(const std::shared_ptr<ov::Data> &chunk, int64_t start_timestamp, double duration_ms, bool independent, bool last_chunk, const std::vector<std::shared_ptr<Marker>> &markers = {});

		uint64_t GetMaxPartialDurationMs() const override;
		uint64_t GetMinPartialDurationMs() const override;

		ov::String GetContainerExtension() const override { return "m4s"; }

		double GetTargetSegmentDuration() const;

	private:
		std::shared_ptr<FMP4Segment> GetSegmentInternal(int64_t segment_number) const;
		std::shared_ptr<FMP4Segment> GetLastSegmentInternal() const;
		

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

		Config	_config;

		std::shared_ptr<const MediaTrack> _track;

		std::shared_ptr<ov::Data> _initialization_section = nullptr;
		
		// segment number : segment
		std::map<int64_t, std::shared_ptr<FMP4Segment>> _segments;
		mutable std::shared_mutex _segments_lock;

		int64_t _initial_segment_number = 0;
		int64_t _start_timestamp_delta = -1;

		double _max_chunk_duration_ms = 0;
		double _min_chunk_duration_ms = static_cast<double>(std::numeric_limits<uint64_t>::max());

		double _target_segment_duration_ms = 0;

		double _total_segment_duration_ms = 0;
		double _total_expected_duration_ms = 0;

		std::shared_ptr<FMp4StorageObserver> _observer;

		ov::String _stream_tag;

		// for making CUE-OUT-CONT
		bool _is_cue_out_on = false;
		uint32_t _cue_out_duration_msec = 0;
		uint32_t _cue_out_elapsed_msec = 0;
	};
}