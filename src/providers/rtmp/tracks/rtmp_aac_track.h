//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2025 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include "./rtmp_audio_track.h"

namespace pvd::rtmp
{
	class RtmpStreamV2;

	class RtmpAacTrack : public RtmpAudioTrack
	{
	public:
		RtmpAacTrack(
			std::shared_ptr<RtmpStreamV2> stream,
			uint32_t track_id,
			bool from_ex_header)
			: RtmpAudioTrack(std::move(stream), track_id, from_ex_header, cmn::MediaCodecId::Aac, cmn::BitstreamFormat::AAC_RAW)
		{
		}

		//--------------------------------------------------------------------
		// Implementation of RtmpTrack
		//--------------------------------------------------------------------
		bool Handle(
			const std::shared_ptr<const modules::rtmp::Message> &message,
			const modules::flv::ParserCommon &parser,
			const std::shared_ptr<const modules::flv::CommonData> &data) override;

		void FillMediaTrackMetadata(const std::shared_ptr<MediaTrack> &media_track) override;

	protected:
		std::optional<modules::flv::SoundSize> _sound_size;
	};
}  // namespace pvd::rtmp
