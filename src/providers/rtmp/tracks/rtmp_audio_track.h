//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2025 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include "./rtmp_track.h"

namespace pvd::rtmp
{
	class RtmpAudioTrack : public RtmpTrack
	{
	public:
		RtmpAudioTrack(
			std::shared_ptr<RtmpStreamV2> stream,
			uint32_t track_id,
			bool from_ex_header,
			cmn::MediaCodecId codec_id,
			cmn::BitstreamFormat bitstream_format)
			: RtmpTrack(std::move(stream), track_id, from_ex_header, codec_id, bitstream_format, cmn::Timebase(1, 1000))
		{
		}

		//--------------------------------------------------------------------
		// Implementation of RtmpTrack
		//--------------------------------------------------------------------
		cmn::MediaType GetMediaType() const override
		{
			return cmn::MediaType::Audio;
		}
	};
}  // namespace pvd::rtmp
