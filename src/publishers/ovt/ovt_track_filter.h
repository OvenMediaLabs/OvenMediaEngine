//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2026 OvenMediaLabs. All rights reserved.
//
//==============================================================================
#pragma once

#include <base/mediarouter/media_type.h>
#include <modules/ovt_packetizer/ovt_wire.h>

#include <cstdint>
#include <optional>
#include <set>

// What one edge may receive from a stream. Every input is an intersection, so composing them has no
// special cases: the TrackSet named in the URL (when given), the removal of every track whose codec an
// OVT1 edge cannot carry, and the set the edge asked for in its PLAY request. With none, nothing is
// filtered. The third input exists only on a PLAY: DESCRIBE runs before the edge knows what is there.
struct OvtTrackFilter
{
	bool legacy_edge = false;
	std::optional<std::set<uint32_t>> track_set_ids;
	std::optional<std::set<uint32_t>> requested_track_ids;

	bool IsActive() const
	{
		return legacy_edge || track_set_ids.has_value() || requested_track_ids.has_value();
	}

	// Whether one track passes every term. An id the edge named that is not on the stream never
	// reaches here, which is how a selection made against a describe the stream has since moved past
	// narrows the set instead of failing the request.
	// The describe path makes the same judgment on the `codec` strings of the JSON it is about to send,
	// so that the tracks it selects cannot diverge from the body it describes.
	bool Accepts(uint32_t track_id, cmn::MediaType media_type, cmn::MediaCodecId codec_id) const
	{
		if (track_set_ids.has_value() && (track_set_ids->count(track_id) == 0))
		{
			return false;
		}

		if (requested_track_ids.has_value() && (requested_track_ids->count(track_id) == 0))
		{
			return false;
		}

		// An OVT1 edge only gets codecs it has a wire value for. `Data` tracks carry no codec and stay.
		if (legacy_edge && (media_type != cmn::MediaType::Data) && (ovt::HasLegacyWireValue(codec_id) == false))
		{
			return false;
		}

		return true;
	}
};
