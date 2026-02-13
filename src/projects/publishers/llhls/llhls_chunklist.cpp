//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2022 AirenSoft. All rights reserved.
//
//==============================================================================

#include "llhls_chunklist.h"
#include "llhls_private.h"
#include <base/ovcrypto/base_64.h>
#include <base/ovlibrary/zip.h>

LLHlsChunklist::LLHlsChunklist(const ov::String &url, const std::shared_ptr<const MediaTrack> &track, 
							uint32_t segment_count, uint32_t target_duration, double part_target_duration, 
							const ov::String &map_uri, bool preload_hint_enabled)
{
	_url = url;
	_track = track;
	_target_duration = target_duration;
	_max_segment_count = segment_count;
	_max_part_duration = 0;
	_part_target_duration = part_target_duration;
	_map_uri = map_uri;
	_preload_hint_enabled = preload_hint_enabled;

	logtt("LLHLS Chunklist has been created. track(%s)", _track->GetVariantName().CStr());
}

LLHlsChunklist::~LLHlsChunklist()
{
	logtt("Chunklist has been deleted. %s", GetTrack()->GetVariantName().CStr());
}

// Set all renditions info for ABR
void LLHlsChunklist::SetRenditions(const std::map<int32_t, std::shared_ptr<LLHlsChunklist>> &renditions)
{
	// lock
	std::lock_guard<std::shared_mutex> lock(_renditions_guard);
	_renditions = renditions;
}

void LLHlsChunklist::EnableCenc(const bmff::CencProperty &cenc_property)
{
	_cenc_property = cenc_property;
}

void LLHlsChunklist::Release()
{
	// lock
	std::lock_guard<std::shared_mutex> lock(_renditions_guard);
	_renditions.clear();
}

void LLHlsChunklist::SetEndList()
{
	_end_list = true;

	UpdateCacheForDefaultChunklist();
}

void LLHlsChunklist::SetWallclockOffset(int64_t offset_ms)
{
	_wallclock_offset_ms = offset_ms;
}

int64_t LLHlsChunklist::GetWallclockOffset() const
{
	return _wallclock_offset_ms;
}

void LLHlsChunklist::SaveOldSegmentInfo(bool enable)
{
	_keep_old_segments = enable;

	if (_keep_old_segments == false)
	{
		std::lock_guard<std::shared_mutex> lock(_segments_guard);
		_old_segments.clear();
	}
}

const std::shared_ptr<const MediaTrack> &LLHlsChunklist::GetTrack() const
{
	return _track;
}

const ov::String& LLHlsChunklist::GetUrl() const
{
	return _url;
}

void LLHlsChunklist::SetPartHoldBack(const float &part_hold_back)
{
	_part_hold_back = part_hold_back;
}

bool LLHlsChunklist::CreateSegmentInfo(const SegmentInfo &info)
{
	logtt("UpdateSegmentInfo[Track : %s/%s]: %s", _track->GetPublicName().CStr(), _track->GetVariantName().CStr(), info.ToString().CStr());

	// Lock
	std::unique_lock<std::shared_mutex> lock(_segments_guard);
	// Create segment
	auto segment = std::make_shared<SegmentInfo>(info);
	_segments.emplace(segment->GetSequence(), segment);

	return true;
}

bool LLHlsChunklist::AppendPartialSegmentInfo(uint32_t segment_sequence, const SegmentInfo &info)
{
	std::shared_ptr<SegmentInfo> segment = GetSegmentInfo(segment_sequence);
	if (segment == nullptr)
	{
		logte("Could not find segment info. segment(%d)", segment_sequence);
		return false;
	}

	{
		std::lock_guard<std::shared_mutex> lock(_segments_guard);
		// part duration is calculated on first segment
		if (_first_segment == true)
		{
			_max_part_duration = std::max(_max_part_duration, info.GetDuration());
		}

		if (info.IsCompleted() == true)
		{
			segment->SetCompleted();
			_last_completed_segment_sequence = segment_sequence;
			_first_segment = false;
		}
	
		_last_segment_sequence = segment_sequence;
		_last_partial_segment_sequence = info.GetSequence();

		segment->InsertPartialSegmentInfo(std::make_shared<SegmentInfo>(info));
	}

	UpdateCacheForDefaultChunklist();

	return true;
}

bool LLHlsChunklist::RemoveSegmentInfo(uint32_t segment_sequence)
{
	std::unique_lock<std::shared_mutex> lock(_segments_guard);

	logtt("RemoveSegmentInfo[Track : %s/%s]: %u", _track->GetPublicName().CStr(), _track->GetVariantName().CStr(), segment_sequence);

	if (_segments.empty())
	{
		return false;
	}

	auto old_segment = _segments.begin()->second;
	if (old_segment->GetSequence() != segment_sequence)
	{
		logtc("The sequence number of the segment to be deleted is not the first segment. segment(%u) first(%" PRId64 ")", segment_sequence, old_segment->GetSequence());
		return false;
	}

	SaveOldSegmentInfo(old_segment);

	_segments.erase(segment_sequence);

	return true;
}

void LLHlsChunklist::UpdateCacheForDefaultChunklist()
{
	// no query string, no skip, no legacy, all segments
	ov::String chunklist = MakeChunklist("", false, false, true);
	{
		// lock 
		std::lock_guard<std::shared_mutex> lock(_cached_default_chunklist_guard);
		_cached_default_chunklist = chunklist;
	}

	{
		// lock 
		std::lock_guard<std::shared_mutex> lock(_cached_default_chunklist_gzip_guard);
		_cached_default_chunklist_gzip = ov::Zip::CompressGzip(chunklist.ToData(false));
	}
}

bool LLHlsChunklist::SaveOldSegmentInfo(std::shared_ptr<SegmentInfo> &segment_info)
{
	if (_keep_old_segments == false)
	{
		return true;
	}

	// no longer need partial segment info - for memory saving
	segment_info->ClearPartialSegments();

	logtt("Save old segment info: %d / %s", segment_info->GetSequence(), segment_info->GetUrl().CStr());

	_old_segments.emplace(segment_info->GetSequence(), segment_info);

	//TODD[CRITICAL](Getroot): _old_segments must be saved to file because memory should be exhausted

	return true;
}

std::shared_ptr<LLHlsChunklist::SegmentInfo> LLHlsChunklist::GetLastSegmentInfo() const
{
	// lock
	std::shared_lock<std::shared_mutex> lock(_segments_guard);

	if (_segments.empty())
	{
		return nullptr;
	}

	return _segments.rbegin()->second;
}

std::shared_ptr<LLHlsChunklist::SegmentInfo> LLHlsChunklist::GetSegmentInfo(uint32_t segment_sequence) const
{
	// lock
	std::shared_lock<std::shared_mutex> lock(_segments_guard);

	auto it = _segments.find(segment_sequence);
	if (it == _segments.end())
	{
		return nullptr;
	}

	return it->second;
}

bool LLHlsChunklist::GetLastSequenceNumber(int64_t &msn, int64_t &psn) const
{
	std::shared_lock<std::shared_mutex> lock(_segments_guard);
	msn = _last_segment_sequence;
	psn = _last_partial_segment_sequence;
	return true;
}

ov::String LLHlsChunklist::MakeExtXKey() const
{
	ov::String xkey;

	for (const auto &pssh : _cenc_property.pssh_box_list)
	{
		if (pssh.drm_system == bmff::DRMSystem::Widevine)
		{
			xkey.AppendFormat("#EXT-X-KEY:");

			if (_cenc_property.scheme == bmff::CencProtectScheme::Cbcs)
			{
				xkey.AppendFormat("METHOD=SAMPLE-AES");
			}
			else if (_cenc_property.scheme == bmff::CencProtectScheme::Cenc)
			{
				xkey.AppendFormat("METHOD=SAMPLE-AES-CTR");
			}

			xkey.AppendFormat(",URI=\"data:text/plain;base64,%s\"", ov::Base64::Encode(pssh.pssh_box_data, false).CStr());

			xkey.AppendFormat(",KEYID=0x%s", _cenc_property.key_id->ToHexString().CStr());
			xkey.AppendFormat(",KEYFORMAT=\"urn:uuid:%s\"", ov::ToUUIDString(pssh.system_id->GetData(), pssh.system_id->GetLength()).LowerCaseString().CStr());
			xkey.AppendFormat(",KEYFORMATVERSIONS=\"1\"");
		}
		else if (pssh.drm_system == bmff::DRMSystem::FairPlay)
		{
			if (_cenc_property.keyformat.LowerCaseString() == "identity")
			{
				xkey.AppendFormat("#EXT-X-KEY:METHOD=SAMPLE-AES");
				xkey.AppendFormat(",URI=\"%s\"", _cenc_property.fairplay_key_uri.CStr());
				xkey.AppendFormat(",KEYFORMAT=\"identity\"");
				xkey.AppendFormat(",IV=0x%s", _cenc_property.iv->ToHexString().CStr());
			}
			else
			{
				xkey.AppendFormat("#EXT-X-KEY:METHOD=SAMPLE-AES");
				xkey.AppendFormat(",URI=\"%s\"", _cenc_property.fairplay_key_uri.CStr());
				xkey.AppendFormat(",KEYFORMAT=\"com.apple.streamingkeydelivery\"");
				xkey.AppendFormat(",KEYFORMATVERSIONS=\"1\"");
			}
		}

		xkey.Append("\n");
	}

	return xkey;
}

ov::String LLHlsChunklist::MakeChunklist(const ov::String &query_string, bool skip, bool legacy, bool rewind, bool vod, uint32_t vod_start_segment_number) const
{
	std::shared_lock<std::shared_mutex> segment_lock(_segments_guard);
	uint8_t version = 6;
	if (_segments.size() == 0)
	{
		return "";
	}

	if (_end_list == true)
	{
		vod = true;
	}

	if (vod == true)
	{
		// VoD doesn't need Low-Latency HLS
		legacy = true;
	}

	// TODO(Getroot) : Implement _HLS_skip=YES (skip = true)

	ov::String playlist(20480);

	playlist.AppendFormat("#EXTM3U\n");

	// debug info
	// playlist.AppendFormat("#// query_string(%s) skip(%s) legacy(%s) rewind(%s) vod(%s) vod_start_segment_number(%u)\n", 
	//  					query_string.CStr(), skip ? "YES" : "NO", legacy ? "YES" : "NO", rewind ? "YES" : "NO", vod ? "YES" : "NO", vod_start_segment_number);


	if (legacy == true)
	{
		version = 6;
	}
	playlist.AppendFormat("#EXT-X-VERSION:%d\n", version);
	// Note that in protocol version 6, the semantics of the EXT-
	// X-TARGETDURATION tag changed slightly.  In protocol version 5 and
	// earlier it indicated the maximum segment duration; in protocol
	// version 6 and later it indicates the the maximum segment duration
	// rounded to the nearest integer number of seconds.
	auto target_duration = static_cast<uint32_t>(std::round(_target_duration));
	playlist.AppendFormat("#EXT-X-TARGETDURATION:%u\n", target_duration);

	// Low Latency Mode
	if (legacy == false)
	{
		// X-SERVER-CONTROL
		playlist.AppendFormat("#EXT-X-SERVER-CONTROL:CAN-BLOCK-RELOAD=YES,PART-HOLD-BACK=%f\n", _part_hold_back);
		playlist.AppendFormat("#EXT-X-PART-INF:PART-TARGET=%lf\n", _part_target_duration);
	}
	else
	{
		// X-PLAYLIST-TYPE
		// playlist.AppendFormat("#EXT-X-SERVER-CONTROL:HOLD-BACK=%u\n", target_duration * 3);
	}

	std::shared_ptr<LLHlsChunklist::SegmentInfo> first_segment = nullptr;
	auto last_segment = _segments.rbegin()->second;
	if (last_segment == nullptr)
	{
		// no segment info
		return "";
	}

	// skip empty segment info 
	if (last_segment->GetPartialSegmentsCount() == 0)
	{
		if (_segments.size() > 1)
		{
			last_segment = std::prev(_segments.end(), 2)->second;
		}
	}

	if (rewind == true)
	{
		first_segment = _segments.begin()->second;
	}
	else if (vod == true)
	{
		// find vod_start_segment_number
		auto it = _segments.find(vod_start_segment_number);
		if (it != _segments.end())
		{
			first_segment = it->second;
		}
		else
		{
			// vod_start_segment_number should be in old_segments
			first_segment = _segments.begin()->second;
		}
	}
	else
	{
		// max segment count is _segment_count
		uint32_t segment_size = _segments.size();
		if (last_segment->IsCompleted() == false)
		{
			segment_size -= 1;
		}

		uint32_t shift_count = segment_size > _max_segment_count ? _max_segment_count : segment_size - 1;
		auto it = _segments.find(_last_completed_segment_sequence - shift_count);
		if (it == _segments.end())
		{
			logte("Could not find segment info. last_completed_segment_sequence(%lld) segment_count(%d)", _last_completed_segment_sequence.load(), _max_segment_count);
			return "";
		}

		first_segment = it->second;
	}

	playlist.AppendFormat("#EXT-X-MEDIA-SEQUENCE:%u\n", vod == false ? first_segment->GetSequence() : 0);

	if (_map_uri.IsEmpty() == false)
	{
		playlist.AppendFormat("#EXT-X-MAP:URI=\"%s", _map_uri.CStr());
		if (query_string.IsEmpty() == false)
		{
			playlist.AppendFormat("?%s", query_string.CStr());
		}
		playlist.AppendFormat("\"\n");
	}

	// CENC
	if (_cenc_property.scheme != bmff::CencProtectScheme::None)
	{
		playlist.AppendFormat("%s\n", MakeExtXKey().CStr());
	}

	if (vod == true)
	{
		for (auto &[number, segment] : _old_segments)
		{
			if (number < vod_start_segment_number)
			{
				continue;
			}

			std::chrono::system_clock::time_point tp{std::chrono::milliseconds{segment->GetStartTime()}};
			playlist.AppendFormat("#EXT-X-PROGRAM-DATE-TIME:%s\n", ov::Converter::ToISO8601String(tp).CStr());
			playlist.AppendFormat("#EXTINF:%lf,\n", segment->GetDuration());
			playlist.AppendFormat("%s", segment->GetUrl().CStr());
			if (query_string.IsEmpty() == false)
			{
				playlist.AppendFormat("?%s", query_string.CStr());
			}
			playlist.Append("\n");
		}
	}

	// from first_segment to last_segment
	for (auto it = _segments.find(first_segment->GetSequence()) ; it != _segments.end(); it ++)
	{
		auto number = it->first;
		auto segment = it->second;

		if (vod == true && number < vod_start_segment_number)
		{
			continue;
		}

		if (segment->GetPartialSegmentsCount() == 0)
		{
			continue;
		}

		if (legacy == true && segment->IsCompleted() == false)
		{
			continue;
		}

		std::chrono::system_clock::time_point tp{std::chrono::milliseconds{segment->GetStartTime()}};
		playlist.AppendFormat("#EXT-X-PROGRAM-DATE-TIME:%s\n", ov::Converter::ToISO8601String(tp).CStr());

		logtt("MakeChunklist[Track : %s/%s]: segment(%d) duration(%.2f) url(%s) start_time(%lld) date_time(%s)",
			_track->GetPublicName().CStr(), _track->GetVariantName().CStr(),
			segment->GetSequence(), segment->GetDuration(), segment->GetUrl().CStr(), segment->GetStartTime(), 
			ov::Converter::ToISO8601String(tp).CStr());

		// Low Latency Mode
		if (legacy == false)
		{
			// Output partial segments info
			// Only output partial segments for the last 4 segments.
			if (segment->GetSequence() > last_segment->GetSequence() - 3)
			{
				for (auto &partial_segment : segment->GetPartialSegments())
				{
					playlist.AppendFormat("#EXT-X-PART:DURATION=%lf,URI=\"%s",
										partial_segment->GetDuration(), partial_segment->GetUrl().CStr());
					if (query_string.IsEmpty() == false)
					{
						playlist.AppendFormat("?%s", query_string.CStr());
					}
					playlist.AppendFormat("\"");
					if (partial_segment->IsIndependent() == true)
					{
						playlist.AppendFormat(",INDEPENDENT=YES");
					}

					playlist.Append("\n");

					//If it is the last one, output PRELOAD-HINT
					if (_preload_hint_enabled == true &&
						segment->GetSequence() == last_segment->GetSequence() &&
						partial_segment == segment->GetPartialSegments().back())
					{
						playlist.AppendFormat("#EXT-X-PRELOAD-HINT:TYPE=PART,URI=\"%s", partial_segment->GetNextUrl().CStr());
						if (query_string.IsEmpty() == false)
						{
							playlist.AppendFormat("?%s", query_string.CStr());
						}
						playlist.AppendFormat("\"\n");
					}
				}
			}
		}

		// Don't print Completed segment info if it is the last segment
		// It will be printed when the next segment is created.
		if ((legacy == true && segment->IsCompleted()) || 
			(legacy == false && segment->IsCompleted() && segment->GetSequence() != last_segment->GetSequence()))
		{
			playlist.AppendFormat("#EXTINF:%lf,\n", segment->GetDuration());
			playlist.AppendFormat("%s", segment->GetUrl().CStr());
			if (query_string.IsEmpty() == false)
			{
				playlist.AppendFormat("?%s", query_string.CStr());
			}
			playlist.Append("\n");
		}
	}
	segment_lock.unlock();

#if 1
	// only for live and low-latency mode
	if (vod == false && legacy == false)
	{
		// Output #EXT-X-RENDITION-REPORT
		// lock
		std::shared_lock<std::shared_mutex> rendition_lock(_renditions_guard);
		for (const auto &[track_id, rendition] : _renditions)
		{
			// Skip mine 
			if (track_id == static_cast<int32_t>(_track->GetId()))
			{
				continue;
			}
			// Skip another media type
			if (rendition->GetTrack()->GetMediaType() != _track->GetMediaType())
			{
				continue;
			}

			playlist.AppendFormat("#EXT-X-RENDITION-REPORT:URI=\"%s", rendition->GetUrl().CStr());
			if (query_string.IsEmpty() == false)
			{
				playlist.AppendFormat("?%s", query_string.CStr());
			}
			playlist.AppendFormat("\"");

			// LAST-MSN, LAST-PART
			int64_t last_msn, last_part;
			rendition->GetLastSequenceNumber(last_msn, last_part);

			if (legacy == true && last_msn > 0)
			{
				// https://datatracker.ietf.org/doc/html/draft-pantos-hls-rfc8216bis#section-4.4.5.4
				// If the Rendition contains Partial Segments then this value is the Media Sequence Number of the last Partial Segment. 

				// In legacy, the completed msn is reported.
				last_msn -= 1;
			}

			playlist.AppendFormat(",LAST-MSN=%llu", last_msn);

			if (legacy == false)
			{
				playlist.AppendFormat(",LAST-PART=%llu", last_part);
			}
			
			playlist.AppendFormat("\n");
		}
	}
#endif

	if (vod == true)
	{
		playlist.AppendFormat("#EXT-X-ENDLIST\n");
	}

	return playlist;
}

ov::String LLHlsChunklist::ToString(const ov::String &query_string, bool skip, bool legacy, bool rewind, bool vod, uint32_t vod_start_segment_number) const
{
	if (_segments.size() == 0)
	{
		return "";
	}

	if (query_string.IsEmpty() && skip == false && legacy == false && rewind == true && vod == false && vod_start_segment_number == 0 && !_cached_default_chunklist.IsEmpty())
	{
		// return cached chunklist for default chunklist
		std::shared_lock<std::shared_mutex> lock(_cached_default_chunklist_guard);
		return _cached_default_chunklist;
	}

	return MakeChunklist(query_string, skip, legacy, rewind, vod, vod_start_segment_number);
}

std::shared_ptr<const ov::Data> LLHlsChunklist::ToGzipData(const ov::String &query_string, bool skip, bool legacy, bool rewind) const
{
	if (query_string.IsEmpty() && skip == false && legacy == false && _cached_default_chunklist_gzip != nullptr && rewind == true)
	{
		std::shared_lock<std::shared_mutex> lock(_cached_default_chunklist_gzip_guard);
		return _cached_default_chunklist_gzip;
	}

	return ov::Zip::CompressGzip(ToString(query_string, skip, legacy, rewind).ToData(false));
}
