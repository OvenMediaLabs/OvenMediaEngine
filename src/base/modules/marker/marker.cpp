//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2025 AirenSoft. All rights reserved.
//
//==============================================================================
#include "marker.h"
#include "marker_private.h"

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

void Marker::SetRealizedTimestamp(int64_t timestamp, int64_t timestamp_ms)
{
	_timestamp = timestamp;
	_timestamp_ms = timestamp_ms;

	// SCTE-35 tags render their times from the event itself, so it moves along
	auto scte_event = GetScte35Event();
	if (scte_event != nullptr)
	{
		scte_event->SetTimestampMsec(timestamp_ms);
	}
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

bool Marker::IsProvisional() const
{
	if (_marker_format == cmn::BitstreamFormat::SCTE35)
	{
		auto scte_event = GetScte35Event();
		return (scte_event != nullptr) ? scte_event->IsProvisional() : false;
	}
	else if (_marker_format == cmn::BitstreamFormat::CUE)
	{
		auto cue_event = GetCueEvent();
		return (cue_event != nullptr) ? cue_event->IsProvisional() : false;
	}

	return false;
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
			tag += ov::String::FormatString(",SCTE35-OUT=0x%s", scte_data->ToHexString().CStr());
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

			// END-DATE: the IN position, so the daterange closes exactly where
			// the break ends
			tp = std::chrono::system_clock::time_point{std::chrono::milliseconds{scte_event->GetTimestampMsec() + timestamp_offset}};
			tag += ov::String::FormatString(",END-DATE=\"%s\"", ov::Converter::ToISO8601String(tp).CStr());

			// DURATION: from START-DATE to END-DATE
			tag += ov::String::FormatString(",DURATION=%.3f", static_cast<double>(scte_event->GetTimestampMsec() - parent_scte_event->GetTimestampMsec()) / 1000.0);

			tag += ov::String::FormatString(",SCTE35-IN=0x%s", scte_data->ToHexString().CStr());
		}

		tag += "\n";
		return tag;
	}

	return "";
}
