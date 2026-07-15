//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2026 OvenMediaLabs. All rights reserved.
//
//==============================================================================
#include "./rtmp_av1_track.h"

namespace pvd::rtmp
{
	// `RtmpAv1Track` defers all width/height extraction to `MediaRouterNormalize::ProcessAV1OBUStream()`,
	// so this translation unit is intentionally minimal - mirroring `RtmpHevcTrack`, which has no overrides.
	// The `cmn::MediaCodecId::Av1` + `cmn::BitstreamFormat::AV1_OBU` association is set by the base constructor.
}  // namespace pvd::rtmp
