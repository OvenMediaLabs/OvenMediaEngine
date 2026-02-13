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
	std::shared_ptr<CueEvent> GetCueEvent() const;
	std::shared_ptr<Scte35Event> GetScte35Event() const;
	ov::String ToHlsTag(int64_t timestamp_offset = 0) const;

	void SetParent(const std::shared_ptr<Marker> &parent);
	std::shared_ptr<Marker> GetParent() const;

	void SetDesiredSequenceNumber(int64_t sequence_number);
	int64_t GetDesiredSequenceNumber() const;

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

	int64_t _desireded_sequence_number = -1;
};

class MarkerBox
{
public:
	bool InsertMarker(const std::shared_ptr<Marker> &marker);
	std::tuple<bool, ov::String> CanInsertMarker(const std::shared_ptr<Marker> &marker) const;

	int64_t GetCurrentSequenceNumber() const;
	int64_t GetEstimatedSequenceNumber(int64_t timestamp_ms) const;

protected:
	bool HasMarker() const;
	// Has marker with the given range
	bool HasMarker(int64_t start_timestamp, int64_t end_timestamp) const;
	bool HasMarker(int64_t end_timestamp) const;
	bool HasMarkerWithSeq(int64_t sequence_number) const;
	uint32_t GetMarkerCount() const;
	const std::shared_ptr<Marker> GetFirstMarker() const;
	std::vector<std::shared_ptr<Marker>> PopMarkers(int64_t start_timestamp, int64_t end_timestamp);
	std::vector<std::shared_ptr<Marker>> PopMarkers(int64_t end_timestamp);
	bool RemoveMarker(int64_t timestamp);
	// remove expired markers
	void RemoveExpiredMarkers(int64_t current_timestamp);
	double GetActualTargetSegmentDurationMs() const;

	struct SegmentationInfo
	{
		// 1/1000 scale 
		cmn::MediaType media_type = cmn::MediaType::Unknown;

		double last_sample_timestamp_ms = 0;
		double last_sample_duration_ms = 0;
		
		bool is_last_segment_completed = false;
		int64_t last_segment_number = -1;
		int64_t last_partial_segment_number = -1;
		double last_segement_duration_ms = 0;
		
		double keyframe_interval = 1.0; // 29, 30, 31...
		double framerate = 0.0;
		double target_segment_duration_ms = 0;
	};

	virtual std::optional<SegmentationInfo> GetSegmentationInfo() const { return std::nullopt; }

private:
	std::map<int64_t, std::shared_ptr<Marker>> _markers_by_timestamp;
	std::map<int64_t, std::shared_ptr<Marker>> _markers_by_sequence_number;
	mutable std::shared_mutex _markers_guard;
	std::shared_ptr<Marker> _last_inserted_marker;
};