//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2026 OvenMediaLabs. All rights reserved.
//
//==============================================================================
#pragma once

#include <base/ovlibrary/ovlibrary.h>
#include <modules/containers/bmff/cenc.h>

namespace pub::llhls
{
	enum class PlaylistType
	{
		// master playlist, uses `#EXT-X-SESSION-KEY`
		Master,
		// media playlist (chunklist), uses `#EXT-X-KEY`
		Media,
	};

	// Builds the HLS key tag(s) for the given CENC property, one line per pssh box.
	ov::String MakeCencKeyTag(const bmff::CencProperty &cenc_property, PlaylistType playlist_type);
}  // namespace pub::llhls
