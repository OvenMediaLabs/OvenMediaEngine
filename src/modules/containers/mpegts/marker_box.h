//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2025 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <base/modules/marker/marker.h>

#include <atomic>
#include <map>
#include <shared_mutex>

namespace mpegts
{
	// The pending SCTE-35/CUE markers of one TS packager: validates the OUT/IN
	// chain and hands the markers over as the segmentation reaches them
	class MarkerBox
	{
	public:
		bool InsertMarker(const std::shared_ptr<Marker> &marker);
		std::tuple<bool, ov::String> CanInsertMarker(const std::shared_ptr<Marker> &marker) const;

		// The cadence this packager cuts at: the segment duration rounded up to
		// whole keyframe intervals (the interval may be unknown, 0). A break must
		// span at least one of it to return from, and a position passed by more
		// than one is beyond saving. The checks stay permissive until this is set.
		void SetCutCadenceMs(double segment_duration_ms, double keyframe_interval_ms);

		// Where this packager's media stands, so a position it has already
		// passed by more than a segment is refused instead of cutting far from
		// the time the marker advertises
		void SetMediaPositionMs(double media_position_ms);

	protected:
		bool HasMarker(int64_t end_timestamp) const;
		std::vector<std::shared_ptr<Marker>> PopMarkers(int64_t end_timestamp);

	private:
		std::map<int64_t, std::shared_ptr<Marker>> _markers_by_timestamp;
		mutable std::shared_mutex _markers_guard;
		std::shared_ptr<Marker> _last_inserted_marker;

		std::atomic<double> _cut_cadence_ms{-1.0};
		// Written from the media thread, read by the insertion path
		std::atomic<double> _media_position_ms{-1.0};
	};
}  // namespace mpegts
