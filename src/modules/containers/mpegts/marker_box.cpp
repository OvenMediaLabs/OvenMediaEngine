//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2025 AirenSoft. All rights reserved.
//
//==============================================================================
#include "marker_box.h"

#include <cmath>

#include "mpegts_private.h"

namespace mpegts
{
	std::tuple<bool, ov::String> MarkerBox::CanInsertMarker(const std::shared_ptr<Marker> &marker) const
	{
		std::shared_lock<std::shared_mutex> lock(_markers_guard);

		auto curr_out_of_network = marker->IsOutOfNetwork();
		if (curr_out_of_network.has_value() == false)
		{
			return {false, ov::String::FormatString("Failed to get out of network value of the marker : %s", marker->GetTag().CStr())};
		}
		bool curr_out_of_network_value = curr_out_of_network.value();

		// The media passed the position by more than a segment: the cut would land
		// nowhere near the time the marker advertises
		const double media_position_ms = _media_position_ms.load(std::memory_order_relaxed);
		if (media_position_ms >= 0 && _cut_cadence_ms > 0 &&
			(media_position_ms - static_cast<double>(marker->GetTimestampMs())) > _cut_cadence_ms)
		{
			return {false, ov::String::FormatString("The position of the marker has already passed by more than the cut cadence : %s (%" PRId64 " ms)", marker->GetTag().CStr(), marker->GetTimestampMs())};
		}

		// A break must be long enough to return from; without a configured cadence
		// the duration and spacing checks stay permissive
		auto required_gap = _cut_cadence_ms * 1.5;
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
				bool last_still_pending = _markers_by_timestamp.find(_last_inserted_marker->GetTimestamp()) != _markers_by_timestamp.end();

				if (last_still_pending == true && _last_inserted_marker->GetTimestamp() >= marker->GetTimestamp())
				{
					// An earlier IN cancels the pending one; the same timestamp is a
					// duplicate of the same return point and replaces it harmlessly.
					// Once it has been emitted there is no open break left to modify
				}
				else if (_last_inserted_marker->IsProvisional() == true && last_still_pending == true && marker->IsProvisional() == false)
				{
					// Only an explicit IN may move a pending provisional return point
					// later; a provisional one is the companion of a rejected OUT and
					// must not extend the open break
				}
				else
				{
					return {false, "IN marker can only be modified while it is pending, with an equal or earlier timestamp"};
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
				if (_last_inserted_marker->GetTimestamp() >= marker->GetTimestamp() || _last_inserted_marker->IsProvisional() == true)
				{
					// remove the replaced xxx-IN marker (duplicate, earlier return
					// point, or a provisional one being confirmed or moved)
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

		_markers_by_timestamp.emplace(marker->GetTimestamp(), marker);
		_last_inserted_marker = marker;

		return true;
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


	std::vector<std::shared_ptr<Marker>> MarkerBox::PopMarkers(int64_t end_timestamp)
	{
		std::lock_guard<std::shared_mutex> lock(_markers_guard);

		std::vector<std::shared_ptr<Marker>> markers;
		for (auto it = _markers_by_timestamp.begin(); it != _markers_by_timestamp.end();)
		{
			auto marker = it->second;  // copy shared_ptr to prevent use-after-free after erase
			if (marker->GetTimestamp() < end_timestamp)
			{
				markers.push_back(marker);
				it = _markers_by_timestamp.erase(it);
			}
			else
			{
				++it;
			}
		}

		return markers;
	}

	void MarkerBox::SetCutCadenceMs(double segment_duration_ms, double keyframe_interval_ms)
	{
		_cut_cadence_ms = segment_duration_ms;

		if (keyframe_interval_ms > 0)
		{
			double intervals = std::ceil(segment_duration_ms / keyframe_interval_ms);
			_cut_cadence_ms = std::max(_cut_cadence_ms, intervals * keyframe_interval_ms);
		}
	}

	void MarkerBox::SetMediaPositionMs(double media_position_ms)
	{
		_media_position_ms.store(media_position_ms, std::memory_order_relaxed);
	}
}  // namespace mpegts
