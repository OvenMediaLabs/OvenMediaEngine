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
	class RtmpVideoTrack : public RtmpTrack
	{
	public:
		RtmpVideoTrack(
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
			return cmn::MediaType::Video;
		}

		bool Handle(
			const std::shared_ptr<const modules::rtmp::Message> &message,
			const modules::flv::ParserCommon &parser,
			const std::shared_ptr<const modules::flv::CommonData> &data) override;

	protected:
		static cmn::PacketType ToCommonPacketType(modules::flv::VideoPacketType packet_type);
	};
}  // namespace pvd::rtmp
