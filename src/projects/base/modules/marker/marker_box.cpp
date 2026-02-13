//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2025 AirenSoft. All rights reserved.
//
//==============================================================================
#include "marker_box.h"
#include "marker_box_private.h"

///////////////////////////////////
// Marker
///////////////////////////////////

std::shared_ptr<Marker> Marker::CreateMarker(cmn::BitstreamFormat marker_format, int64_t timestamp, int64_t timestamp_ms, const std::shared_ptr<ov::Data> &data)
{
	std::shared_ptr<Marker> marker(new Marker(marker_format, timestamp, timestamp_ms, data));
	if (marker == nullptr)
	{
		return nullptr;
	}

	if (marker->Init() == false)
	{
		return nullptr;
	}

	return marker;
}

Marker::Marker(cmn::BitstreamFormat marker_format, int64_t timestamp, int64_t timestamp_ms, const std::shared_ptr<ov::Data> &data)
{
	_marker_format = marker_format;
	_timestamp = timestamp;
	_timestamp_ms = timestamp_ms;
	_data = data;
}

bool Marker::Init()
{
	ov::String tag_prefix;

	if (_marker_format == cmn::BitstreamFormat::CUE)
	{
		tag_prefix = "CueEvent-";

		auto cue_event = CueEvent::Parse(_data);
		if (cue_event == nullptr)
		{
			return false;
		}

		_event = cue_event;

		// CueEvent-OUT, CueEvent-CONT, CueEvent-IN
		_tag = tag_prefix + cue_event->GetCueTypeName();
	}
	else if (_marker_format == cmn::BitstreamFormat::SCTE35)
	{
		tag_prefix = "Scte35Event-";

		auto scte_event = Scte35Event::Parse(_data);
		if (scte_event == nullptr)
		{
			return false;
		}

		_event = scte_event;

		// Scte35Event-OUT, Scte35Event-IN
		ov::String tag_suffix = scte_event->IsOutOfNetwork() ? "OUT" : "IN";
		_tag = tag_prefix + tag_suffix;
	}
	else
	{
		return false;
	}

	return true;
}

void Marker::SetParent(const std::shared_ptr<Marker> &parent)
{
	_parent = parent;
}

std::shared_ptr<Marker> Marker::GetParent() const
{
	return _parent;
}

// Getter
cmn::BitstreamFormat Marker::GetMarkerFormat() const
{
	return _marker_format;
}

int64_t Marker::GetTimestamp() const
{
	return _timestamp;
}

int64_t Marker::GetTimestampMs() const
{
	return _timestamp_ms;
}

ov::String Marker::GetTag() const
{
	return _tag;
}

std::shared_ptr<ov::Data> Marker::GetData() const
{
	return _data;
}

std::optional<bool> Marker::IsOutOfNetwork() const
{
	if (_marker_format == cmn::BitstreamFormat::SCTE35)
	{
		auto scte_event = GetScte35Event();
		if (scte_event == nullptr)
		{
			return std::nullopt;
		}

		return scte_event->IsOutOfNetwork();
	}
	else if (_marker_format == cmn::BitstreamFormat::CUE)
	{
		auto cue_event = GetCueEvent();
		if (cue_event == nullptr)
		{
			return std::nullopt;
		}

		return cue_event->GetCueType() == CueEvent::CueType::OUT;
	}

	return std::nullopt;
}

void Marker::SetDesiredSequenceNumber(int64_t sequence_number)
{
	_desireded_sequence_number = sequence_number;
}

int64_t Marker::GetDesiredSequenceNumber() const
{
	return _desireded_sequence_number;
}

std::shared_ptr<CueEvent> Marker::GetCueEvent() const
{
	if (_marker_format != cmn::BitstreamFormat::CUE)
	{
		return nullptr;
	}

	return std::get<std::shared_ptr<CueEvent>>(_event);
}

std::shared_ptr<Scte35Event> Marker::GetScte35Event() const
{
	if (_marker_format != cmn::BitstreamFormat::SCTE35)
	{
		return nullptr;
	}

	return std::get<std::shared_ptr<Scte35Event>>(_event);
}

ov::String Marker::ToHlsTag(int64_t timestamp_offset) const
{
	if (_marker_format == cmn::BitstreamFormat::CUE)
	{
		// #EXT-X-CUE-OUT:DURATION=3.000
		// #EXT-X-CUE-IN

		auto cue_event = GetCueEvent();
		if (cue_event == nullptr)
		{
			return "";
		}

		if (cue_event->GetCueType() == CueEvent::CueType::OUT)
		{
			return ov::String::FormatString("#EXT-X-CUE-OUT:DURATION=%.3f\n", static_cast<double>(cue_event->GetDurationMsec()) / 1000.0);
		}
		else if (cue_event->GetCueType() == CueEvent::CueType::CONT)
		{
			return ov::String::FormatString("#EXT-X-CUE-OUT-CONT:ElapsedTime=%.3f,Duration=%.3f\n", static_cast<double>(cue_event->GetElapsedMsec()) / 1000.0, static_cast<double>(cue_event->GetDurationMsec()) / 1000.0);
		}
		else if (cue_event->GetCueType() == CueEvent::CueType::IN)
		{
			return "#EXT-X-CUE-IN\n";
		}
	}
	else if (_marker_format == cmn::BitstreamFormat::SCTE35)
	{
		// #EXT-X-DATERANGE:ID="1234",START-DATE="2025-01-01T00:00:00Z",PLANNED-DURATION=60.0,SCTE35-OUT=0xFC002F000000000000FF
		// #EXT-X-DATERANGE:ID="1234",START-DATE="2025-01-01T00:00:00Z",END-DATE="2025-01-01T00:01:00Z",DURATION=60.0,SCTE35-IN=0xFC002F000000000000FF

		auto scte_event = GetScte35Event();
		if (scte_event == nullptr)
		{
			return "";
		}

		auto scte_data = scte_event->MakeScteData();
		if (scte_data == nullptr)
		{
			return "";
		}

		ov::String tag = "#EXT-X-DATERANGE:";

		// ID
		tag += ov::String::FormatString("ID=\"%u\"", scte_event->GetID());

		if (scte_event->IsOutOfNetwork())
		{
			// START-DATE
			std::chrono::system_clock::time_point tp{std::chrono::milliseconds{scte_event->GetTimestampMsec() + timestamp_offset}};
			tag += ov::String::FormatString(",START-DATE=\"%s\"", ov::Converter::ToISO8601String(tp).CStr());

			// PLANNED-DURATION
			tag += ov::String::FormatString(",PLANNED-DURATION=%.3f", static_cast<double>(scte_event->GetDurationMsec()) / 1000.0);
			tag += ov::String::FormatString(",SCTE35-OUT=%s", scte_data->ToHexString().CStr());
		}
		else
		{
			// START-DATE from the parent
			if (GetParent() == nullptr || GetParent()->GetScte35Event() == nullptr)
			{
				return "";
			}
			auto parent_scte_event = GetParent()->GetScte35Event();
			if (parent_scte_event == nullptr)
			{
				return "";
			}

			// START-DATE
			std::chrono::system_clock::time_point tp{std::chrono::milliseconds{parent_scte_event->GetTimestampMsec() + timestamp_offset}};
			tag += ov::String::FormatString(",START-DATE=\"%s\"", ov::Converter::ToISO8601String(tp).CStr());

			// END-DATE
			// tp = std::chrono::system_clock::time_point{std::chrono::milliseconds{scte_event->GetTimestampMsec() + scte_event->GetDurationMsec() + timestamp_offset}};
			// tag += ov::String::FormatString(",END-DATE=\"%s\"", ov::Converter::ToISO8601String(tp).CStr());

			// // DURATION
			// tag += ov::String::FormatString(",DURATION=%.3f", static_cast<double>(scte_event->GetDurationMsec()) / 1000.0);

			tag += ov::String::FormatString(",SCTE35-IN=%s", scte_data->ToHexString().CStr());
		}

		tag += "\n";
		return tag;
	}

	return "";
}

std::tuple<bool, ov::String> MarkerBox::CanInsertMarker(const std::shared_ptr<Marker> &marker) const
{
	std::shared_lock<std::shared_mutex> lock(_markers_guard);

	auto curr_out_of_network = marker->IsOutOfNetwork();
	if (curr_out_of_network.has_value() == false)
	{
		return {false, ov::String::FormatString("Failed to get out of network value of the marker : %s", marker->GetTag().CStr())};
	}
	bool curr_out_of_network_value = curr_out_of_network.value();

	auto actual_segment_duration_ms = GetActualTargetSegmentDurationMs();
	auto required_gap = actual_segment_duration_ms * 1.5;
	if (curr_out_of_network_value == true)
	{
		// Duration check
		if (marker->GetMarkerFormat() == cmn::BitstreamFormat::CUE)
		{
			auto cue_event = marker->GetCueEvent();
			if (cue_event == nullptr)
			{
				return {false, ov::String::FormatString("Failed to get CueEvent from the marker : %s", marker->GetTag().CStr())};
			}

			auto duration_ms = cue_event->GetDurationMsec();
			if (duration_ms <= required_gap)
			{
				return {false, ov::String::FormatString("Duration of the marker must be greater than %f ms : %s", required_gap, marker->GetTag().CStr())};
			}
		}
		else if (marker->GetMarkerFormat() == cmn::BitstreamFormat::SCTE35)
		{
			auto scte_event = marker->GetScte35Event();
			if (scte_event == nullptr)
			{
				return {false, ov::String::FormatString("Failed to get Scte35Event from the marker : %s", marker->GetTag().CStr())};
			}

			auto duration_ms = scte_event->GetDurationMsec();
			if (duration_ms <= required_gap)
			{
				return {false, ov::String::FormatString("Duration of the marker must be greater than %f ms : %s", required_gap, marker->GetTag().CStr())};
			}
		}
	}

	if (_last_inserted_marker == nullptr)
	{
		if (curr_out_of_network_value == false)
		{
			return {false, "First marker must be OUT marker"};
		}

		// First marker always can be inserted
		return {true, ""};
	}

	auto last_out_of_network = _last_inserted_marker->IsOutOfNetwork();
	if (last_out_of_network.has_value() == false)
	{
		return {false, ov::String("Failed to get out of network value of the last inserted marker")};
	}
	bool last_out_of_network_value = last_out_of_network.value();

	// XXX-OUT marker
	if (curr_out_of_network_value == true)
	{
		// OUT -> OUT
		if (last_out_of_network_value == true)
		{
			return {false, "OUT marker cannot be inserted after OUT marker"};
		}
		// IN -> OUT
		else if (last_out_of_network_value == false && _last_inserted_marker->GetTimestamp() > marker->GetTimestamp())
		{
			return {false, "OUT marker cannot be inserted before IN marker"};
		}
		// IN -> OUT with the small gap
		else if (last_out_of_network_value == false && (marker->GetTimestampMs() - _last_inserted_marker->GetTimestampMs() < required_gap))
		{
			return {false, ov::String::FormatString("OUT marker must be inserted with the gap of %f ms", required_gap)};
		}
	}
	// XXX-IN marker
	else
	{
		// IN -> IN
		if (last_out_of_network_value == false)
		{
			if (_last_inserted_marker->GetTimestamp() > marker->GetTimestamp())
			{
				// IN -> IN with the earlier timestamp, it will cancel the last IN marker
			}
			else
			{
				return {false, "IN marker only can be modified with the less timestamp"};
			}
		}
		// OUT -> IN
		else if (last_out_of_network_value == true && _last_inserted_marker->GetTimestamp() > marker->GetTimestamp())
		{
			return {false, "IN marker cannot be inserted before OUT marker"};
		}
		// OUT -> IN with the small gap
		else if (last_out_of_network_value == true && (marker->GetTimestampMs() - _last_inserted_marker->GetTimestampMs() < required_gap))
		{
			return {false, ov::String::FormatString("IN marker must be inserted with the gap of %f ms", required_gap)};
		}
	}

	return {true, ""};
}

bool MarkerBox::InsertMarker(const std::shared_ptr<Marker> &marker)
{
	auto [can_insert, message] = CanInsertMarker(marker);
	if (can_insert == false)
	{
		logte("Failed to insert the marker : %s", message.CStr());
		return false;
	}

	std::lock_guard<std::shared_mutex> lock(_markers_guard);

	auto curr_out_of_network = marker->IsOutOfNetwork();
	if (curr_out_of_network.has_value() == false)
	{
		logtw("Failed to get out of network value of the marker : %s", marker->GetTag().CStr());
		return false;
	}
	bool curr_out_of_network_value = curr_out_of_network.value();

	if (_last_inserted_marker == nullptr)
	{
		if (curr_out_of_network_value == false)
		{
			logte("First marker must be OUT marker");
			return false;
		}

		_markers_by_timestamp.emplace(marker->GetTimestamp(), marker);
		if (marker->GetDesiredSequenceNumber() >= 0)
		{
			// Update the sequence number
			_markers_by_sequence_number.emplace(marker->GetDesiredSequenceNumber(), marker);
		}

		_last_inserted_marker = marker;
		return true;
	}

	auto last_out_of_network = _last_inserted_marker->IsOutOfNetwork();
	if (last_out_of_network.has_value() == false)
	{
		logtw("Failed to get out of network value of the last inserted marker");
		return false;
	}
	bool last_out_of_network_value = last_out_of_network.value();

	// Cancel the last xxx-IN marker
	if (curr_out_of_network_value == false)
	{
		if (last_out_of_network_value == false)
		{
			if (_last_inserted_marker->GetTimestamp() > marker->GetTimestamp())
			{
				// remove the last CUEEVENT-IN marker
				_markers_by_timestamp.erase(_last_inserted_marker->GetTimestamp());
			}
			
			// Inherit the parent
			marker->SetParent(_last_inserted_marker->GetParent());
		}
		else
		{
			marker->SetParent(_last_inserted_marker);
		}
	}

	if (marker->GetDesiredSequenceNumber() >= 0)
	{
		// Update the sequence number
		_markers_by_sequence_number.emplace(marker->GetDesiredSequenceNumber(), marker);
	}

	_markers_by_timestamp.emplace(marker->GetTimestamp(), marker);
	_last_inserted_marker = marker;

	return true;
}

bool MarkerBox::HasMarker() const
{
	std::shared_lock<std::shared_mutex> lock(_markers_guard);
	return _markers_by_timestamp.empty() == false;
}

bool MarkerBox::HasMarker(int64_t start_timestamp, int64_t end_timestamp) const
{
	std::shared_lock<std::shared_mutex> lock(_markers_guard);
	for (auto &it : _markers_by_timestamp)
	{
		auto &marker = it.second;
		if (marker->GetTimestamp() >= start_timestamp && marker->GetTimestamp() < end_timestamp)
		{
			return true;
		}
	}

	return false;
}

bool MarkerBox::HasMarker(int64_t end_timestamp) const
{
	std::shared_lock<std::shared_mutex> lock(_markers_guard);
	for (auto &it : _markers_by_timestamp)
	{
		auto &marker = it.second;
		if (marker->GetTimestamp() < end_timestamp)
		{
			return true;
		}
	}

	return false;
}

bool MarkerBox::HasMarkerWithSeq(int64_t sequence_number) const
{
	std::shared_lock<std::shared_mutex> lock(_markers_guard);
	return _markers_by_sequence_number.find(sequence_number) != _markers_by_sequence_number.end();
}

const std::shared_ptr<Marker> MarkerBox::GetFirstMarker() const
{
	std::shared_lock<std::shared_mutex> lock(_markers_guard);
	if (_markers_by_timestamp.empty() == true)
	{
		return nullptr;
	}

	return _markers_by_timestamp.begin()->second;
}

std::vector<std::shared_ptr<Marker>> MarkerBox::PopMarkers(int64_t start_timestamp, int64_t end_timestamp)
{
	std::lock_guard<std::shared_mutex> lock(_markers_guard);

	std::vector<std::shared_ptr<Marker>> markers;
	for (auto it = _markers_by_timestamp.begin(); it != _markers_by_timestamp.end();)
	{
		auto &marker = it->second;
		if (marker->GetTimestamp() >= start_timestamp && marker->GetTimestamp() < end_timestamp)
		{
			markers.push_back(marker);
			it = _markers_by_timestamp.erase(it);
			_markers_by_sequence_number.erase(marker->GetDesiredSequenceNumber());
		}
		else
		{
			++it;
		}
	}

	return markers;
}

std::vector<std::shared_ptr<Marker>> MarkerBox::PopMarkers(int64_t end_timestamp)
{
	std::lock_guard<std::shared_mutex> lock(_markers_guard);

	std::vector<std::shared_ptr<Marker>> markers;
	for (auto it = _markers_by_timestamp.begin(); it != _markers_by_timestamp.end();)
	{
		auto &marker = it->second;
		if (marker->GetTimestamp() < end_timestamp)
		{
			markers.push_back(marker);
			it = _markers_by_timestamp.erase(it);
			_markers_by_sequence_number.erase(marker->GetDesiredSequenceNumber());
		}
		else
		{
			++it;
		}
	}

	return markers;
}

uint32_t MarkerBox::GetMarkerCount() const
{
	std::shared_lock<std::shared_mutex> lock(_markers_guard);
	return _markers_by_timestamp.size();
}

bool MarkerBox::RemoveMarker(int64_t timestamp)
{
	std::lock_guard<std::shared_mutex> lock(_markers_guard);

	auto it = _markers_by_timestamp.find(timestamp);
	if (it == _markers_by_timestamp.end())
	{
		return false;
	}

	_markers_by_timestamp.erase(it);
	_markers_by_sequence_number.erase(it->second->GetDesiredSequenceNumber());

	return true;
}

void MarkerBox::RemoveExpiredMarkers(int64_t current_timestamp)
{
	std::lock_guard<std::shared_mutex> lock(_markers_guard);

	for (auto it = _markers_by_timestamp.begin(); it != _markers_by_timestamp.end();)
	{
		auto &marker = it->second;
		if (marker->GetTimestamp() < current_timestamp)
		{
			logtc("Remove expired marker:(%" PRId64 ") %" PRId64 " - %s", current_timestamp, marker->GetTimestamp(), marker->GetTag().CStr());
			it = _markers_by_timestamp.erase(it);
			_markers_by_sequence_number.erase(marker->GetDesiredSequenceNumber());
		}
		else
		{
			++it;
		}
	}
}

int64_t MarkerBox::GetCurrentSequenceNumber() const
{
	auto segment_info = GetSegmentationInfo();
	if (segment_info.has_value() == false)
	{
		return -1;
	}

	return segment_info->last_segment_number;
}

int64_t MarkerBox::GetEstimatedSequenceNumber(int64_t timestamp_ms) const
{
	auto segment_info = GetSegmentationInfo();
	if (segment_info.has_value() == false)
	{
		return -1;
	}

	if (segment_info->media_type != cmn::MediaType::Video)
	{
		return -1;
	}

	auto actual_segment_duration_ms = GetActualTargetSegmentDurationMs();
	auto last_sample_end_timestamp_ms = segment_info->last_sample_timestamp_ms + segment_info->last_sample_duration_ms;
	auto time_until_marker_ms = timestamp_ms - last_sample_end_timestamp_ms;
	auto remaining_time_in_current_segment_ms = segment_info->is_last_segment_completed ? 0 : actual_segment_duration_ms - segment_info->last_segement_duration_ms;


	int64_t estimated_sequence_number = segment_info->last_segment_number;
	
	if (time_until_marker_ms <= remaining_time_in_current_segment_ms)
	{
		// In the current segment
	}
	else 
	{
		estimated_sequence_number += std::ceil((time_until_marker_ms - remaining_time_in_current_segment_ms) / actual_segment_duration_ms);
	}

	// Plus previous markers count, marker makes the sequence number increase
	auto prev_markers = GetMarkerCount();
	estimated_sequence_number += prev_markers;

	logtt("actual_segment_duration_ms: %f, last_sample_end_timestamp_ms: %f, time_until_marker_ms: %f, remaining_time_in_current_segment_ms: %f, estimated_sequence_number: %" PRId64 " last_segment_number: %" PRId64 " last_segment_duration: %f is_last_segment_completed: %d",
		  actual_segment_duration_ms, last_sample_end_timestamp_ms, time_until_marker_ms, remaining_time_in_current_segment_ms, estimated_sequence_number, segment_info->last_segment_number, segment_info->last_segement_duration_ms, segment_info->is_last_segment_completed);

	return estimated_sequence_number;
}

double MarkerBox::GetActualTargetSegmentDurationMs() const
{
	auto segment_info = GetSegmentationInfo();
	if (segment_info.has_value() == false)
	{
		return -1;
	}

	double actual_segment_duration_ms = segment_info->target_segment_duration_ms;

	if (segment_info->media_type != cmn::MediaType::Video)
	{
		auto keyframe_interval_ms = segment_info->keyframe_interval / segment_info->framerate * 1000;
		actual_segment_duration_ms = std::ceil(segment_info->target_segment_duration_ms / keyframe_interval_ms) * keyframe_interval_ms;
	}

	return actual_segment_duration_ms;
}
