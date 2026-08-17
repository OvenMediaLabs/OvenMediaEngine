//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2026 OvenMediaLabs. All rights reserved.
//
//==============================================================================
#pragma once

#include "./rtmp_video_track.h"

namespace pvd::rtmp
{
	class RtmpStreamV2;

	class RtmpAv1Track : public RtmpVideoTrack
	{
	public:
		RtmpAv1Track(std::shared_ptr<RtmpStreamV2> stream, uint32_t track_id, bool from_ex_header)
			: RtmpVideoTrack(std::move(stream), track_id, from_ex_header, cmn::MediaCodecId::Av1, cmn::BitstreamFormat::AV1_OBU)
		{
		}
	};
}  // namespace pvd::rtmp
