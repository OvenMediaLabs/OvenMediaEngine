//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2025 AirenSoft. All rights reserved.
//
//==============================================================================
#include "./rtmp_video_track.h"

#include "../rtmp_provider_private.h"

namespace pvd::rtmp
{
	cmn::PacketType RtmpVideoTrack::ToCommonPacketType(modules::flv::VideoPacketType packet_type)
	{
		switch (packet_type)
		{
			OV_CASE_RETURN(modules::flv::VideoPacketType::SequenceStart, cmn::PacketType::SEQUENCE_HEADER);
			OV_CASE_RETURN(modules::flv::VideoPacketType::CodedFrames, cmn::PacketType::NALU);
			OV_CASE_RETURN(modules::flv::VideoPacketType::CodedFramesX, cmn::PacketType::NALU);

			case modules::flv::VideoPacketType::Metadata:
				// Metadata should be handled in the stream
				OV_ASSERT(false, "VideoPacketType::Metadata should be handled in the RTMP stream");
				[[fallthrough]];
			case modules::flv::VideoPacketType::SequenceEnd:
				[[fallthrough]];
			case modules::flv::VideoPacketType::MPEG2TSSequenceStart:
				[[fallthrough]];
			case modules::flv::VideoPacketType::Multitrack:
				[[fallthrough]];
			case modules::flv::VideoPacketType::ModEx:
				logtt("Not handled video packet type: %s", modules::flv::EnumToString(packet_type));
				return cmn::PacketType::Unknown;
		}

		OV_ASSERT(false, "Unknown VideoPacketType");
		return cmn::PacketType::Unknown;
	}

	bool RtmpVideoTrack::Handle(
		const std::shared_ptr<const modules::rtmp::Message> &message,
		const modules::flv::ParserCommon &parser,
		const std::shared_ptr<const modules::flv::CommonData> &data)
	{
		do
		{
			auto video_data = std::dynamic_pointer_cast<const modules::flv::VideoData>(data);

			if (video_data == nullptr)
			{
				// This should never happen
				OV_ASSERT2(false);
				return false;
			}

			auto packet_type = ToCommonPacketType(video_data->video_packet_type);

			if (packet_type == cmn::PacketType::Unknown)
			{
				logtt("Unknown %s packet type: %d",
					  cmn::GetCodecIdString(_codec_id),
					  static_cast<int>(video_data->video_packet_type));
				break;
			}

			// CTS = PTS - DTS
			const auto cts	= video_data->composition_time_offset;
			const int64_t dts = message->header->completed.timestamp;
			const int64_t pts = cts + dts;

			std::shared_ptr<MediaPacket> media_packet;

			if (packet_type == cmn::PacketType::SEQUENCE_HEADER)
			{
				if (video_data->header == nullptr)
				{
					logte("%s sequence header is not found", cmn::GetCodecIdString(_codec_id));
					OV_ASSERT2(false);
					return false;
				}

				_sequence_header = video_data->header;

				media_packet	 = CreateMediaPacket(
					video_data->header_data,
					pts, dts,
					packet_type,
					true);
			}
			else
			{
				if (video_data->payload == nullptr)
				{
					logte("%s payload is not found", cmn::GetCodecIdString(_codec_id));
					OV_ASSERT2(false);
					return false;
				}

				media_packet = CreateMediaPacket(
					video_data->payload,
					pts, dts,
					packet_type,
					video_data->IsKeyFrame());
			}

			_media_packet_list.push_back(media_packet);
		} while (false);

		return true;
	}
}  // namespace pvd::rtmp
