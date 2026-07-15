//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2025 AirenSoft. All rights reserved.
//
//==============================================================================

#include "webvtt_packager.h"
#include "webvtt_private.h"

namespace webvtt
{
	Packager::Packager(const std::shared_ptr<const MediaTrack> &track)
			: _track(track)
	{
	}

	Packager::~Packager()
	{
		logtt("WebVTT Packager has been terminated finally");
	}

	bool Packager::AddFrame(const std::shared_ptr<WebVTTFrame> &frame)
	{
		if (frame == nullptr)
		{
			return false;
		}

		std::unique_lock<std::shared_mutex> lock(_frames_guard);
		_frames.emplace(frame->GetStartTimeMs(), frame);

		logtd("WebVTT Packager: Added frame: StartTimeMs=%" PRId64 ", EndTimeMs=%" PRId64 ", Text Length=%zu / %s", frame->GetStartTimeMs(), frame->GetEndTimeMs(), frame->GetText().GetLength(), frame->GetText().CStr());

		return true;
	}

	bool Packager::MakeSegment(int64_t segment_number, int64_t start_time_ms, double duration_ms)
	{
		ov::String vtt_text;

		vtt_text = MakeVTTHeader(start_time_ms);
		vtt_text += MakeCueText(start_time_ms, duration_ms, true);

		auto segment = GetSegmentInternal(segment_number);
		if (segment != nullptr)
		{
			segment->Update(start_time_ms, duration_ms, vtt_text);
		}
		else
		{
			segment = std::make_shared<Segment>(segment_number, start_time_ms, duration_ms, vtt_text);
		}

		{
			std::unique_lock<std::shared_mutex> lock(_segments_guard);
			_segments[segment_number] = segment;
		}

		return true;
	}

	bool Packager::MakePartialSegment(int64_t segment_number, int64_t partial_segment_number, int64_t start_time_ms, double duration_ms)
	{
		ov::String vtt_text;

		vtt_text = MakeVTTHeader(start_time_ms);
		vtt_text += MakeCueText(start_time_ms, duration_ms, false);

		// logti("WebVTT Packager: Making partial segment: SegmentNumber=%u, PartialSegmentNumber=%u, StartTimeMs=%" PRId64 ", DurationMs=%" PRId64 ", VTTTextLength=%zu\n\n%s", 
		// 	segment_number, partial_segment_number, start_time_ms, duration_ms, vtt_text.GetLength(), vtt_text.CStr());
		
		auto partial_segment = std::make_shared<PartialSegment>(segment_number, partial_segment_number, start_time_ms, duration_ms, vtt_text);
		partial_segment->SetIndependent(true);

		_max_partial_duration_ms = std::max(_max_partial_duration_ms, static_cast<uint64_t>(duration_ms));
		_min_partial_duration_ms = std::min(_min_partial_duration_ms, static_cast<uint64_t>(duration_ms));

		auto segment = GetSegmentInternal(segment_number);
		if (segment == nullptr)
		{
			segment = std::make_shared<Segment>(segment_number);
			std::unique_lock<std::shared_mutex> lock(_segments_guard);
			_segments[segment_number] = segment;
		}
		
		segment->AddPartialSegment(partial_segment);
		
		return true;
	}

	const ov::String Packager::MakeVTTHeader(int64_t start_time_ms)
	{
		constexpr int64_t kPtsWrap = (1LL << 33); // 33-bit PTS wrap

		ov::String vtt_text;
		vtt_text = "WEBVTT\n";

		// ms → 90kHz PTS (wrap to 33bit)
		int64_t mpegts = (start_time_ms * 90) % kPtsWrap;
		vtt_text += ov::String::FormatString("X-TIMESTAMP-MAP=LOCAL:00:00:00.000,MPEGTS:%" PRId64 "\n", mpegts);

		vtt_text += "\n\n";

		return vtt_text;
	}
	
	const ov::String Packager::MakeCueText(int64_t start_time_ms, double duration_ms, bool remove_used_frames)
	{
		ov::String cue_text;
		int64_t end_time_ms = start_time_ms + static_cast<int64_t>(llround(duration_ms));
		bool expired_frame = false;
		std::unique_lock<std::shared_mutex> lock(_frames_guard);
		for (auto it = _frames.begin(); it != _frames.end();)
		{
			expired_frame = false;
			auto& frame = it->second;
			if (frame == nullptr)
			{
				it = _frames.erase(it);
				continue;
			}

			if (frame->GetStartTimeMs() >= start_time_ms && frame->GetStartTimeMs() < end_time_ms)
			{
				cue_text += frame->ToVttFormatText(start_time_ms);
				cue_text += "\n";
				expired_frame = true;
				frame->MarkAsUsed();
			}
			else if (frame->GetStartTimeMs() < start_time_ms)
			{
				// This frame is expired
				expired_frame = true;
			}
			else if (frame->GetStartTimeMs() >= end_time_ms)
			{
				// Future frame
				break;
			}
			
			if (remove_used_frames && expired_frame)
			{
				if (frame->IsUsed() == false)
				{
					logtw("WebVTT Packager: Removing unused frame: StartTimeMs=%" PRId64 ", EndTimeMs=%" PRId64 ", Text Length=%zu / %s", frame->GetStartTimeMs(), frame->GetEndTimeMs(), frame->GetText().GetLength(), frame->GetText().CStr());
				}

				it = _frames.erase(it);
			}
			else
			{
				++it;
			}
		}

		return cue_text;
	}

	bool Packager::DeleteSegment(int64_t segment_number)
	{
		std::unique_lock<std::shared_mutex> lock(_segments_guard);
		return _segments.erase(segment_number) > 0;
	}

	// SegmentStorage overrides
	std::shared_ptr<ov::Data> Packager::GetInitializationSection() const
	{
		return nullptr;
	}
	std::shared_ptr<base::modules::Segment> Packager::GetSegment(int64_t segment_number) const
	{
		return GetSegmentInternal(segment_number);
	}

	std::shared_ptr<base::modules::Segment> Packager::GetLastSegment() const
	{
		std::shared_lock<std::shared_mutex> lock(_segments_guard);
		if (_segments.empty())
		{
			return nullptr;
		}

		return _segments.rbegin()->second;
	}

	std::shared_ptr<base::modules::PartialSegment> Packager::GetPartialSegment(int64_t segment_number, int64_t partial_segment_number) const 
	{
		return GetPartialSegmentInternal(segment_number, partial_segment_number);
	}

	std::shared_ptr<Segment> Packager::GetSegmentInternal(int64_t segment_number) const
	{
		std::shared_lock<std::shared_mutex> lock(_segments_guard);
		
		auto it = _segments.find(segment_number);
		if (it != _segments.end())
		{
			return it->second;
		}

		return nullptr;
	}

	std::shared_ptr<PartialSegment> Packager::GetPartialSegmentInternal(int64_t segment_number, int64_t partial_segment_number) const
	{
		std::shared_lock<std::shared_mutex> lock(_segments_guard);
		auto it = _segments.find(segment_number);
		if (it != _segments.end())
		{
			return it->second->GetPartialSegment(partial_segment_number);
		}
		
		return nullptr;
	}

	uint64_t Packager::GetSegmentCount() const
	{
		std::shared_lock<std::shared_mutex> lock(_segments_guard);
		return _segments.size();
	}

	int64_t Packager::GetLastSegmentNumber() const
	{
		std::shared_lock<std::shared_mutex> lock(_segments_guard);
		if (_segments.empty())
		{
			return -1;
		}

		return _segments.rbegin()->first;
	}

	std::tuple<int64_t, int64_t> Packager::GetLastPartialSegmentNumber() const
	{
		std::shared_lock<std::shared_mutex> lock(_segments_guard);
		if (_segments.empty())
		{
			return std::make_tuple(-1, -1);
		}

		auto last_segment = _segments.rbegin()->second;
		if (last_segment == nullptr)
		{
			return std::make_tuple(-1, -1);
		}

		auto last_partial = last_segment->GetLastPartialSegment();
		if (last_partial == nullptr)
		{
			return std::make_tuple(last_segment->GetNumber(), -1);
		}
		
		return std::make_tuple(last_segment->GetNumber(), last_partial->GetNumber());
	}

	uint64_t Packager::GetMaxPartialDurationMs() const
	{
		return _max_partial_duration_ms;
	}

	uint64_t Packager::GetMinPartialDurationMs() const
	{
		return _min_partial_duration_ms;
	}
}