//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2025 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include "base/common_types.h"
#include "base/info/dump.h"

#include <base/modules/data_format/cue_event/cue_event.h>
#include <base/modules/data_format/scte35_event/scte35_event.h>


class Marker
{
public:
	static std::shared_ptr<Marker> CreateMarker(cmn::BitstreamFormat marker_format, int64_t timestamp, int64_t timestamp_ms, const std::shared_ptr<ov::Data> &data);

	// Getter
	cmn::BitstreamFormat GetMarkerFormat() const;
	int64_t GetTimestamp() const;
	int64_t GetTimestampMs() const;
	ov::String GetTag() const;
	std::shared_ptr<ov::Data> GetData() const;
	std::optional<bool> IsOutOfNetwork() const;
	// A provisional IN announces the planned return point; while it is pending it
	// may be replaced by an explicit IN, earlier or later
	bool IsProvisional() const;
	std::shared_ptr<CueEvent> GetCueEvent() const;
	std::shared_ptr<Scte35Event> GetScte35Event() const;
	ov::String ToHlsTag(int64_t timestamp_offset = 0) const;

	void SetParent(const std::shared_ptr<Marker> &parent);
	std::shared_ptr<Marker> GetParent() const;

private:
	Marker(cmn::BitstreamFormat marker_format, int64_t timestamp, int64_t timestamp_ms, const std::shared_ptr<ov::Data> &data);
	bool Init();

	cmn::BitstreamFormat _marker_format;
	int64_t _timestamp = -1;
	int64_t _timestamp_ms = -1;
	ov::String _tag;
	std::shared_ptr<ov::Data> _data = nullptr;

	std::variant<std::shared_ptr<CueEvent>, std::shared_ptr<Scte35Event>> _event;

	// If it is a "IN" marker, it has a connected "OUT" marker
	std::shared_ptr<Marker> _parent = nullptr;
};
