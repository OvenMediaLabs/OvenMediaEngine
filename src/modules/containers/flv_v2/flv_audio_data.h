//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2025 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include "./flv_common.h"

namespace modules
{
	namespace flv
	{
		struct AudioData : public CommonData
		{
			AudioData(uint32_t default_track_id, SoundFormat sound_format, bool from_ex_header)
				: CommonData(default_track_id, from_ex_header),
				  sound_format(sound_format)
			{
			}

			const SoundFormat sound_format;

			// Available only when `_is_ex_header == false` (Legacy AAC)
			std::optional<SoundRate> sound_rate;
			std::optional<SoundSize> sound_size;
			std::optional<SoundType> sound_type;

			// Available only when `_is_ex_header == true` (E-RTMP)
			std::optional<AudioPacketType> audio_packet_type;
			std::optional<AudioPacketModExType> audio_packet_mod_ex_type;
			std::optional<uint24_t> audio_timestamp_nano_offset;

			std::optional<AudioFourCc> audio_fourcc;

			std::optional<AudioChannelOrder> audio_channel_order;
			std::optional<uint8_t> channel_count;
			std::optional<AudioChannel> audio_channel_mapping;
			std::optional<uint32_t> audio_channel_flags;

			// AAC only
			AACPacketType aac_packet_type;
		};
	}  // namespace flv
}  // namespace modules
