//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2025 AirenSoft. All rights reserved.
//
//==============================================================================
#include "./rtmp_track.h"

#include "../rtmp_provider_private.h"
#include "../rtmp_stream_v2.h"
#include "./rtmp_aac_track.h"
#include "./rtmp_avc_track.h"
#include "./rtmp_hevc_track.h"

namespace pvd::rtmp
{
	std::shared_ptr<RtmpTrack> RtmpTrack::Create(std::shared_ptr<RtmpStreamV2> stream, uint32_t track_id, bool from_ex_header, cmn::MediaCodecId codec_id)
	{
		switch (codec_id)
		{
			OV_CASE_RETURN(cmn::MediaCodecId::None, nullptr);
			OV_CASE_RETURN(cmn::MediaCodecId::H264, std::make_shared<RtmpAvcTrack>(stream, track_id, from_ex_header));
			OV_CASE_RETURN(cmn::MediaCodecId::H265, std::make_shared<RtmpHevcTrack>(stream, track_id, from_ex_header));
			OV_CASE_RETURN(cmn::MediaCodecId::Vp8, nullptr);
			OV_CASE_RETURN(cmn::MediaCodecId::Vp9, nullptr);
			OV_CASE_RETURN(cmn::MediaCodecId::Av1, nullptr);
			OV_CASE_RETURN(cmn::MediaCodecId::Flv, nullptr);
			OV_CASE_RETURN(cmn::MediaCodecId::Aac, std::make_shared<RtmpAacTrack>(stream, track_id, from_ex_header));
			OV_CASE_RETURN(cmn::MediaCodecId::Mp3, nullptr);
			OV_CASE_RETURN(cmn::MediaCodecId::Opus, nullptr);
			OV_CASE_RETURN(cmn::MediaCodecId::Jpeg, nullptr);
			OV_CASE_RETURN(cmn::MediaCodecId::Png, nullptr);
			OV_CASE_RETURN(cmn::MediaCodecId::Webp, nullptr);
			OV_CASE_RETURN(cmn::MediaCodecId::WebVTT, nullptr);
			OV_CASE_RETURN(cmn::MediaCodecId::Whisper, nullptr);
		}

		OV_ASSERT2(false);
		return nullptr;
	}

	info::NamePath RtmpTrack::GetNamePath() const
	{
		return _stream->GetNamePath();
	}

	std::shared_ptr<MediaPacket> RtmpTrack::CreateMediaPacket(
		const std::shared_ptr<const ov::Data> &payload,
		int64_t pts, int64_t dts,
		cmn::PacketType packet_type,
		bool is_key_frame)
	{
		auto media_packet = std::make_shared<MediaPacket>(
			_stream->GetMsid(),
			GetMediaType(),
			_track_id,
			payload,
			pts,
			dts,
			-1LL,
			MediaPacketFlag::Unknown,
			_bitstream_format,
			packet_type);

		media_packet->SetFlag(is_key_frame ? MediaPacketFlag::Key : MediaPacketFlag::NoFlag);

		return media_packet;
	}

	void RtmpTrack::FillMediaTrackMetadata(const std::shared_ptr<MediaTrack> &media_track)
	{
		media_track->SetId(_track_id);
		media_track->SetMediaType(GetMediaType());
		media_track->SetCodecId(_codec_id);
		media_track->SetOriginBitstream(_bitstream_format);
		media_track->SetTimeBase(_time_base);
	}

}  // namespace pvd::rtmp
