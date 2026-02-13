//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2022 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <base/ovlibrary/ovlibrary.h>
#include <base/info/media_track.h>
#include <base/mediarouter/media_buffer.h>
#include <base/modules/marker/marker_box.h>

#include "modules/containers/bmff/cenc.h"

class LLHlsChunklist
{
public:
	class SegmentInfo
	{
	public:
		SegmentInfo(uint32_t sequence)
			: _sequence(sequence)
		{
		}

		// For Segment
		SegmentInfo(uint32_t sequence, ov::String url)
			: _sequence(sequence)
			, _start_time(0)
			, _duration(0)
			, _size(0)
			, _url(url)
			, _next_url("")
			, _is_independent(true)
			, _completed(false)
		{
		}

		// For Partial Segment
		SegmentInfo(uint32_t sequence, int64_t start_time, double duration, uint64_t size, ov::String url, ov::String next_url, bool is_independent, bool completed)
			: _sequence(sequence)
			, _start_time(start_time)
			, _duration(duration)
			, _size(size)
			, _url(url)
			, _next_url(next_url)
			, _is_independent(is_independent)
			, _completed(completed)
		{
		}

		void UpdateInfo(int64_t start_time, double duration, uint64_t size, ov::String url, bool is_independent)
		{
			this->_start_time = start_time;
			this->_duration = duration;
			this->_size = size;
			this->_url = url;
			this->_is_independent = is_independent;
		}

		int64_t GetStartTime() const
		{
			if (_partial_segments.empty() == false)
			{
				return _partial_segments[0]->GetStartTime();
			}

			return _start_time;
		}

		int64_t GetSequence() const
		{
			return _sequence;
		}

		double GetDuration() const
		{
			return _duration;
		}

		uint64_t GetSize() const
		{
			return _size;
		}

		ov::String GetUrl() const
		{
			return _url;
		}

		ov::String GetNextUrl() const
		{
			return _next_url;
		}

		bool IsIndependent() const
		{
			return _is_independent;
		}

		// Insert partial segment info
		bool InsertPartialSegmentInfo(const std::shared_ptr<SegmentInfo> &partial_segment)
		{
			if (partial_segment == nullptr)
			{
				return false;
			}

			if (_partial_segments.empty() == true)
			{
				// first partial segment
				_start_time = partial_segment->GetStartTime();
			}

			_duration += partial_segment->GetDuration();

			if (partial_segment->HasMarker())
			{
				SetMarkers(partial_segment->GetMarkers());
			}

			_partial_segments.push_back(partial_segment);

			return true;
		}

		// Get partial segments
		const std::deque<std::shared_ptr<SegmentInfo>> &GetPartialSegments() const
		{
			return _partial_segments;
		}

		// Get Partial segments count
		uint32_t GetPartialSegmentsCount() const
		{
			return _partial_segments.size();
		}

		// Clear partial segments
		void ClearPartialSegments()
		{
			_partial_segments.clear();
			_partial_segments.shrink_to_fit();
		}

		void SetCompleted()
		{
			_completed = true;
		}

		bool IsCompleted() const
		{
			return _completed;
		}

		ov::String GetStartDate() const
		{
			ov::String start_date;

			// Convert start time to date
			{
				time_t start_time = _start_time / 1000;
				struct tm *tm = localtime(&start_time);
				char date[64];
				strftime(date, sizeof(date), "%Y-%m-%dT%H:%M:%S", tm);
				start_date = date;
			}

			return start_date;
		}

		bool HasMarker() const
		{
			return _markers.empty() == false;
		}

		void SetMarkers(const std::vector<std::shared_ptr<Marker>> &markers)
		{
			_markers = markers;
		}

		const std::vector<std::shared_ptr<Marker>> &GetMarkers() const
		{
			return _markers;
		}

		ov::String ToString() const
		{
			return ov::String::FormatString("seq(%" PRId64") start_date(%s) duration(%f) size(%" PRIu64 ") url(%s) next_url(%s) is_independent(%s) completed(%s)", _sequence, GetStartDate().CStr(), _duration, _size, _url.CStr(), _next_url.CStr(), _is_independent ? "true" : "false", _completed ? "true" : "false");
		}

	private:
		int64_t _sequence = -1;
		int64_t _start_time = 0; // milliseconds since epoch (1970-01-01 00:00:00)
		double _duration = 0; // seconds
		uint64_t _size = 0;
		ov::String _url;
		ov::String _next_url;
		bool _is_independent = false;
		bool _completed = false;

		std::deque<std::shared_ptr<SegmentInfo>> _partial_segments;

		std::vector<std::shared_ptr<Marker>> _markers;
	}; // class SegmentInfo

	LLHlsChunklist(const ov::String &url, const std::shared_ptr<const MediaTrack> &track, 
					uint32_t segment_count, uint32_t target_duration, double part_target_duration, 
					const ov::String &map_uri, bool preload_hint_enabled);

	~LLHlsChunklist();

	void EnableCenc(const bmff::CencProperty &cenc_property);

	// A LLHlsChunklist has circular dependency issues because it holds its own pointer and pointers to all other chunklists. 
	// Therefore, you must call the Release function.
	void Release();

	const ov::String& GetUrl() const;

	// Set all renditions info for ABR
	void SetRenditions(const std::map<int32_t, std::shared_ptr<LLHlsChunklist>> &renditions);

	void SaveOldSegmentInfo(bool enable);

	// Get Track
	const std::shared_ptr<const MediaTrack> &GetTrack() const;

	void SetPartHoldBack(const float &part_hold_back);

	bool CreateSegmentInfo(const SegmentInfo &info);
	bool AppendPartialSegmentInfo(uint32_t segment_sequence, const SegmentInfo &info);
	bool RemoveSegmentInfo(uint32_t segment_sequence);

	ov::String ToString(const ov::String &query_string, bool skip, bool legacy, bool rewind, bool vod = false, uint32_t vod_start_segment_number = 0) const;
	std::shared_ptr<const ov::Data> ToGzipData(const ov::String &query_string, bool skip, bool legacy, bool rewind) const;

	std::shared_ptr<SegmentInfo> GetSegmentInfo(uint32_t segment_sequence) const;
	bool GetLastSequenceNumber(int64_t &msn, int64_t &psn) const;

	void SetEndList();

	void SetWallclockOffset(int64_t offset_ms);
	int64_t GetWallclockOffset() const;

private:
	std::shared_ptr<SegmentInfo> GetLastSegmentInfo() const;

	bool SaveOldSegmentInfo(std::shared_ptr<SegmentInfo> &segment_info);

	ov::String MakeChunklist(const ov::String &query_string, bool skip, bool legacy, bool rewind, bool vod = false, uint32_t vod_start_segment_number = 0) const;

	ov::String MakeExtXKey() const;

	ov::String MakeMarkers(const std::vector<std::shared_ptr<Marker>> &markers) const;

	std::shared_ptr<const MediaTrack> _track;

	ov::String _url;

	double _target_duration = 0;
	uint32_t _max_segment_count = 0;
	double _part_target_duration = 0;
	double _max_part_duration = 0;
	double _part_hold_back = 0;
	ov::String _map_uri;
	bool _preload_hint_enabled = true;

	std::atomic<int64_t> _last_segment_sequence = -1;
	std::atomic<int64_t> _last_completed_segment_sequence = -1;
	std::atomic<int64_t> _last_partial_segment_sequence = -1;

	// Segment number, SegmentInfo
	std::map<int64_t, std::shared_ptr<SegmentInfo>> _segments;

	// old_segments is for only HLS dump
	std::map<int64_t, std::shared_ptr<SegmentInfo>> _old_segments;
	mutable std::shared_mutex _segments_guard;

	bool _keep_old_segments = false;

	bool _first_segment = true;

	std::map<int32_t, std::shared_ptr<LLHlsChunklist>> _renditions;
	mutable std::shared_mutex _renditions_guard;

	ov::String _cached_default_chunklist;
	mutable std::shared_mutex _cached_default_chunklist_guard;

	std::shared_ptr<ov::Data> _cached_default_chunklist_gzip;
	mutable std::shared_mutex _cached_default_chunklist_gzip_guard;

	bmff::CencProperty _cenc_property;

	bool _end_list = false;

	int64_t _wallclock_offset_ms = 0;

	std::shared_ptr<Marker> _root_marker;

	void UpdateCacheForDefaultChunklist();
};