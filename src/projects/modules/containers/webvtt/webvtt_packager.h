//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2025 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <base/info/media_track.h>
#include <base/mediarouter/media_buffer.h>
#include <base/modules/marker/marker_box.h>
#include <base/modules/container/segment_storage.h>

#include "base/modules/data_format/webvtt/webvtt_frame.h"

namespace webvtt
{
	class PartialSegment : public base::modules::PartialSegment
	{
	public:
		PartialSegment(int64_t segment_number, int64_t partial_segment_number, int64_t start_time, double duration_ms, const ov::String &vtt_text)
			: _segment_number(segment_number), _partial_segment_number(partial_segment_number), _start_time(start_time), _duration_ms(duration_ms), _vtt_text(vtt_text)
		{
		}

		int64_t GetNumber() const override { return _partial_segment_number; }
		int64_t GetStartTimestamp() const override { return _start_time; }
		double GetDurationMs() const override { return _duration_ms; }
		bool IsIndependent() const override { return _independent; }
		uint64_t GetDataLength() const override { return _vtt_text.GetLength(); }
		const std::shared_ptr<ov::Data> GetData() const override { return _vtt_text.ToData(false); }

		void SetIndependent(bool independent) { _independent = independent; }

		double GetTimebaseSeconds() const override { return 1.0/1000.0; } 

		void SetUrl(const ov::String &url) override { _url = url; }
		ov::String GetUrl() const override { return _url; }

	private:
		int64_t _segment_number;
		int64_t _partial_segment_number;
		int64_t _start_time;
		double _duration_ms;
		bool _independent = false;
		ov::String _vtt_text; // Serialized VTT text
		ov::String _url;
	};

	class Segment : public base::modules::Segment
	{
	public:
		Segment(int64_t segment_number)
			: _segment_number(segment_number), _start_time(0), _duration_ms(0)
		{
			_completed = false;
		}

		Segment(int64_t segment_number, int64_t start_time, double duration_ms, const ov::String &vtt_text)
			: _segment_number(segment_number), _start_time(start_time), _duration_ms(duration_ms), _vtt_text(vtt_text)
		{
			_completed = true;
		}

		const ov::String &GetVTTText() const { return _vtt_text; }

		int64_t GetNumber() const override { return _segment_number; }
		int64_t GetStartTimestamp() const override { return _start_time; }
		double GetDurationMs() const override { return _duration_ms; }
		bool IsIndependent() const override { return true; }
		uint64_t GetDataLength() const override { return _vtt_text.GetLength(); }
		const std::shared_ptr<ov::Data> GetData() const override { return _vtt_text.ToData(false); }
		bool IsCompleted() const override { return true; }
		bool HasMarker() const override { return false; }
		double GetTimebaseSeconds() const override { return 1.0/1000.0; } 

		void SetUrl(const ov::String &url) override { _url = url; }
		ov::String GetUrl() const override { return _url; }
		
		const std::vector<std::shared_ptr<Marker>> &GetMarkers() const override 
		{ 
			static std::vector<std::shared_ptr<Marker>> empty; 
			return empty; 
		}

		bool Update(int64_t start_time, double duration_ms, const ov::String &vtt_text)
		{
			_start_time = start_time;
			_duration_ms = duration_ms;
			_vtt_text = vtt_text;
			_completed = true;
			return true;
		}

		bool AddPartialSegment(const std::shared_ptr<webvtt::PartialSegment> &partial_segment)
		{
			std::unique_lock<std::shared_mutex> lock(_partial_segments_guard);
			if (partial_segment == nullptr)
			{
				return false;
			}

			if (_partial_segments.size() == 0)
			{
				// First partial segment
				_start_time = partial_segment->GetStartTimestamp();
			}

			_partial_segments[partial_segment->GetNumber()] = partial_segment;
			return true;
		}

		const std::shared_ptr<webvtt::PartialSegment> GetPartialSegment(int64_t partial_segment_number) const
		{
			std::shared_lock<std::shared_mutex> lock(_partial_segments_guard);
			auto it = _partial_segments.find(partial_segment_number);
			if (it != _partial_segments.end())
			{
				return it->second;
			}

			return nullptr;
		}

		const std::shared_ptr<webvtt::PartialSegment> GetLastPartialSegment() const
		{
			std::shared_lock<std::shared_mutex> lock(_partial_segments_guard);
			if (_partial_segments.empty())
			{
				return nullptr;
			}

			return _partial_segments.rbegin()->second;
		}

		uint32_t GetPartialSegmentCount() const
		{
			std::shared_lock<std::shared_mutex> lock(_partial_segments_guard);
			return _partial_segments.size();
		}

	private:
		int64_t _segment_number;
		int64_t _start_time;
		double _duration_ms;
		ov::String _vtt_text; // Serialized VTT text

		bool _completed = false;

		// partial segments
		std::map<int64_t, std::shared_ptr<webvtt::PartialSegment>> _partial_segments;
		mutable std::shared_mutex _partial_segments_guard;

		ov::String _url;
	};

	class Packager : public base::modules::SegmentStorage
	{
	public:
		Packager(const std::shared_ptr<const MediaTrack> &track);
		virtual ~Packager();

		bool AddFrame(const std::shared_ptr<WebVTTFrame> &frame);
		
		bool MakeSegment(int64_t segment_number, int64_t start_time_ms, int64_t duration_ms);
		bool MakePartialSegment(int64_t segment_number, int64_t partial_segment_number, int64_t start_time_ms, int64_t duration_ms);
		bool DeleteSegment(int64_t segment_number);

		// SegmentStorage overrides
		std::shared_ptr<ov::Data> GetInitializationSection() const override;
		std::shared_ptr<base::modules::Segment> GetSegment(int64_t segment_number) const override;
		std::shared_ptr<base::modules::Segment> GetLastSegment() const override;
		std::shared_ptr<base::modules::PartialSegment> GetPartialSegment(int64_t segment_number, int64_t partial_segment_number) const override;
		uint64_t GetSegmentCount() const override;
		int64_t GetLastSegmentNumber() const override;
		std::tuple<int64_t, int64_t> GetLastPartialSegmentNumber() const override;
		uint64_t GetMaxPartialDurationMs() const override;
		uint64_t GetMinPartialDurationMs() const override;
		ov::String GetContainerExtension() const override { return "vtt"; }

	private:
		std::shared_ptr<Segment> GetSegmentInternal(int64_t segment_number) const;
		std::shared_ptr<PartialSegment> GetPartialSegmentInternal(int64_t segment_number, int64_t partial_segment_number) const;

		const ov::String MakeVTTHeader(int64_t start_time_ms);
		const ov::String MakeCueText(int64_t start_time_ms, int64_t duration_ms, bool remove_used_frames);

		// start_time_ms : frame
		// To make ordered
		std::map<int64_t, std::shared_ptr<WebVTTFrame>> _frames;
		std::shared_mutex _frames_guard;

		std::map<int64_t, std::shared_ptr<Segment>> _segments;
		mutable std::shared_mutex _segments_guard;

		uint64_t _max_partial_duration_ms = 0;
		uint64_t _min_partial_duration_ms = std::numeric_limits<uint64_t>::max();

		std::shared_ptr<const MediaTrack> _track;
	};
}