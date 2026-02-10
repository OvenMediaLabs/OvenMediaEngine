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
#include "fmp4_private.h"

namespace bmff
{
	FMP4Storage::FMP4Storage(const std::shared_ptr<FMp4StorageObserver> &observer, const std::shared_ptr<const MediaTrack> &track, const FMP4Storage::Config &config, const ov::String &stream_tag)
	{
		_config = config;
		_track = track;
		_observer = observer;

		// Keep one more to prevent download failure due to timing issue
		_target_segment_duration_ms = static_cast<int64_t>(_config.segment_duration_ms);
		_stream_tag = stream_tag;

		_initial_segment_number = 0;
		if (_config.server_time_based_segment_numbering == true)
		{
			// last segment number = current epoch time / segment duration
			_initial_segment_number = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count() / _target_segment_duration_ms;
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

	std::shared_ptr<ov::Data> FMP4Storage::GetInitializationSection() const
	{
		return _initialization_section;
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
			segment = GetSegmentInternal(segment_number + 1);
			if (segment == nullptr)
			{
				return nullptr;
			}

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
		_initialization_section = section;
		if (_observer != nullptr)
		{
			_observer->OnFMp4StorageInitialized(_track->GetId());
		}
		return true;
	}

	double FMP4Storage::GetTargetSegmentDuration() const
	{
		return _target_segment_duration_ms;
	}

	ov::String FMP4Storage::GetDVRDirectory() const
	{
		return ov::String::FormatString("%s/%s/%d", _config.dvr_storage_path.CStr(), _stream_tag.CStr(), _track->GetId());
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
				_observer->OnMediaSegmentDeleted(_track->GetId(), segment_to_delete.segment_number);
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

	std::shared_ptr<FMP4Segment> FMP4Storage::CreateNextSegment()
	{
		// Create next segment
		auto segment = std::make_shared<FMP4Segment>(GetLastSegmentNumber() + 1, _config.segment_duration_ms, _track->GetTimeBase().GetExpr());
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
						_observer->OnMediaSegmentDeleted(_track->GetId(), old_segment->GetNumber());
					}
				}
			}
		}

		if (_observer != nullptr)
		{
			_observer->OnMediaSegmentCreated(_track->GetId(), segment->GetNumber());
		}

		return segment;
	}

	bool FMP4Storage::AppendMediaChunk(const std::shared_ptr<ov::Data> &chunk, int64_t start_timestamp, double duration_ms, bool independent, bool last_chunk, const std::vector<std::shared_ptr<Marker>> &markers)
	{
		auto segment = GetLastSegmentInternal();
		if (segment == nullptr || segment->IsCompleted() == true)
		{
			segment = CreateNextSegment();
		}

		if (segment->AppendPartialData(chunk, start_timestamp, duration_ms, independent) == false)
		{
			return false;
		}

		segment->AddMarkers(markers);

		if (segment->GetDurationMs() > _config.segment_duration_ms * 2)
		{
			// Force to complete the segment
			last_chunk = true;
			// Too long segment buffered
			logte("LLHLS stream (%s) / track (%d) - the duration of the segment being created exceeded twice the target segment duration (%.1lf ms | expected: %" PRIu64 ") because there were no IDR frames for a long time. This segment is forcibly created and may not play normally.", 
			_stream_tag.CStr(), _track->GetId(), segment->GetDurationMs(), _config.segment_duration_ms);
		}

		// Complete Segment if segment duration is over and new chunk data is independent(new segment should be started with independent chunk)
		if (last_chunk == true)
		{
			segment->SetCompleted();
			CreateNextSegment();

			logtt("Segment[%" PRId64 "] is created : track(%u), duration(%f) chunks(%lu)", segment->GetNumber(), _track->GetId(),segment->GetDurationMs(), segment->GetPartialCount());
			
			_total_expected_duration_ms += _config.segment_duration_ms;
			_total_segment_duration_ms += segment->GetDurationMs();

			// When there is a marker, it comes out smaller than or equal to the expected Segment.
			// Depending on the conditions, Audio may come out smaller or equal, but Video may come out smaller, equal or larger.
			// If Audio comes out smaller and Video comes out the equal, the Sequnce is broken.
			// Therefore, in this case, the algorithm is configured to come out smaller unconditionally.
			if (segment->HasMarker() == true)
			{
				logti("LLHLS stream (%s) / track (%u) - segment[%" PRId64 "] has markers %s", _stream_tag.CStr(), _track->GetId(), segment->GetNumber(), segment->GetMarkers().back()->GetTag().CStr());

				_total_expected_duration_ms -= _config.segment_duration_ms;
				
				// auto last_marker = segment->GetMarkers().back();
				// if (last_marker != nullptr && last_marker->IsOutOfNetwork() == true)
				// {
				// 	// We can initialize the all time variables here
				// 	_total_expected_duration_ms = 0;
				// 	_total_segment_duration_ms = 0;
				// }
				// else if (last_marker != nullptr && last_marker->IsOutOfNetwork() == false)
				// {
				// 	_total_expected_duration_ms -= _config.segment_duration_ms;
				// }
			}

			double next_target_duration = _total_expected_duration_ms - _total_segment_duration_ms + _config.segment_duration_ms;

			logtt("LLHLS stream (%s) / track (%u) - segment_seq(%" PRId64 ") segment_duration_ms: %f total_expected_duration_ms: %f, total_segment_duration_ms: %f, next_target_duration: %f",
				_stream_tag.CStr(), _track->GetId(), segment->GetNumber(), segment->GetDurationMs(), _total_expected_duration_ms, _total_segment_duration_ms, next_target_duration);
			
			if (next_target_duration >= static_cast<double>(_config.segment_duration_ms)/2.0)
			{
				_target_segment_duration_ms = next_target_duration;
			}
			else
			{
				_target_segment_duration_ms = next_target_duration; //static_cast<double>(_config.segment_duration_ms / 2);
			}

			// Make CUE-OUT-CONT
			// TODO(Getroot): Later it will be added with server option
			// if (segment->HasMarker())
			// {
			// 	for (const auto &it : segment->GetMarkers())
			// 	{
			// 		if (it.tag.UpperCaseString() == "CUEEVENT-OUT")
			// 		{
			// 			// Get duration
			// 			auto cue_out_event = ::CueEvent::Parse(it.data);
			// 			if (cue_out_event != nullptr)
			// 			{
			// 				_is_cue_out_on = true;
			// 				_cue_out_duration_msec = cue_out_event->GetDurationMsec();
			// 				_cue_out_elapsed_msec = 0;
			// 			}

			// 		}
			// 		else if (it.tag.UpperCaseString() == "CUEEVENT-IN")
			// 		{
			// 			_is_cue_out_on = false;
			// 			_cue_out_duration_msec = 0;
			// 			_cue_out_elapsed_msec = 0;
			// 		}
			// 	}
			// }
			// else if (segment->HasMarker() == false && _is_cue_out_on == true)
			// {
			// 	_cue_out_elapsed_msec += segment->GetDuration();

			// 	// Make CUE-OUT-CONT
			// 	Marker marker;
			// 	marker.timestamp = segment->GetStartTimestamp() + segment->GetDuration();
			// 	marker.tag = "CUEEVENT-OUT-CONT";
			// 	marker.data = CueEvent::Create(::CueEvent::CueType::CONT, _cue_out_duration_msec, _cue_out_elapsed_msec)->Serialize();

			// 	segment->SetMarkers({ marker });
			// }

			logtt("LLHLS stream (%s) / track (%u) - segment_duration_ms: %f total_expected_duration_ms: %f, total_segment_duration_ms: %f, next_target_duration: %f, target_segment_duration: %f has_marker: %d",
				_stream_tag.CStr(), _track->GetId(), segment->GetDurationMs(), _total_expected_duration_ms, _total_segment_duration_ms, next_target_duration, _target_segment_duration_ms, segment->HasMarker());
		}

		_max_chunk_duration_ms = std::max(_max_chunk_duration_ms, duration_ms);
		_min_chunk_duration_ms = std::min(_min_chunk_duration_ms, duration_ms);

		// Notify observer
		if (_observer != nullptr)
		{
			bool last_chunk = segment->IsCompleted() == true;
			_observer->OnMediaChunkUpdated(_track->GetId(), segment->GetNumber(), segment->GetLastPartialNumber(), last_chunk);
		}

		return true;
	}
} // namespace bmff