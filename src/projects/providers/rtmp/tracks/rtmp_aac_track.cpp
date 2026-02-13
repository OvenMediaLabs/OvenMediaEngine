//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2025 AirenSoft. All rights reserved.
//
//==============================================================================
#include "./rtmp_aac_track.h"

#include <modules/rtmp_v2/chunk/rtmp_datastructure.h>

#include "../rtmp_provider_private.h"
#include "../rtmp_stream_v2.h"

namespace pvd::rtmp
{
	cmn::PacketType ToCommonPacketType(modules::flv::AACPacketType packet_type)
	{
		switch (packet_type)
		{
			OV_CASE_RETURN(modules::flv::AACPacketType::SequenceHeader, cmn::PacketType::SEQUENCE_HEADER);
			OV_CASE_RETURN(modules::flv::AACPacketType::Raw, cmn::PacketType::RAW);
		}

		OV_ASSERT(false, "Unknown AACPacketType");
		return cmn::PacketType::Unknown;
	}

	bool RtmpAacTrack::Handle(
		const std::shared_ptr<const modules::rtmp::Message> &message,
		const modules::flv::ParserCommon &parser,
		const std::shared_ptr<const modules::flv::CommonData> &data)
	{
		do
		{
			auto audio_data = std::dynamic_pointer_cast<const modules::flv::AudioData>(data);

			if (audio_data == nullptr)
			{
				// This should never happen
				OV_ASSERT2(false);
				return false;
			}

			auto packet_type = ToCommonPacketType(audio_data->aac_packet_type);

			if (packet_type == cmn::PacketType::Unknown)
			{
				logtt("Unknown AAC packet type: %d", static_cast<int>(audio_data->aac_packet_type));
				break;
			}

			const int64_t dts = message->header->completed.timestamp;
			const int64_t pts = dts;

			std::shared_ptr<MediaPacket> media_packet;

			if (audio_data->from_ex_header == false)
			{
				auto &sound_size = audio_data->sound_size;
				_sound_size		 = sound_size.has_value() ? sound_size.value() : _sound_size;
			}

			if (packet_type == cmn::PacketType::SEQUENCE_HEADER)
			{
				if (audio_data->header == nullptr)
				{
					logte("%s sequence header is not found", cmn::GetCodecIdString(_codec_id));
					OV_ASSERT2(false);
					return false;
				}

				_sequence_header = audio_data->header;

				media_packet	 = CreateMediaPacket(
					audio_data->header_data,
					pts, dts,
					packet_type,
					true);
			}
			else
			{
				if (audio_data->payload == nullptr)
				{
					logte("%s payload is not found", cmn::GetCodecIdString(_codec_id));
					OV_ASSERT2(false);
					return false;
				}

				media_packet = CreateMediaPacket(
					audio_data->payload,
					pts, dts,
					packet_type,
					true);
			}

			_media_packet_list.push_back(media_packet);
		} while (false);

		return true;
	}

	void RtmpAacTrack::FillMediaTrackMetadata(const std::shared_ptr<MediaTrack> &media_track)
	{
		RtmpTrack::FillMediaTrackMetadata(media_track);

		auto &sound_size   = _sound_size;
		auto sample_format = cmn::AudioSample::Format::S16;

		if (sound_size.has_value())
		{
			switch (sound_size.value())
			{
				OV_CASE_BREAK(modules::flv::SoundSize::_8Bit, sample_format = cmn::AudioSample::Format::U8);
				OV_CASE_BREAK(modules::flv::SoundSize::_16Bit, sample_format = cmn::AudioSample::Format::S16);
			}
		}

		media_track->GetSample().SetFormat(sample_format);
	}
}  // namespace pvd::rtmp
