//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2025 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include "./flv_audio_data.h"

namespace modules
{
	namespace flv
	{
		class AudioParser : public ParserCommon
		{
		public:
			AudioParser(uint32_t default_track_id)
				: ParserCommon(default_track_id)
			{
			}

			bool Parse(ov::BitReader &reader) override;

			OV_DEFINE_CONST_GETTER(GetDataList, _data_list, noexcept);

		protected:
			// Parse legacy AudioTagHeader (AAC only)
			MAY_THROWS(BitReaderError)
			bool ParseLegacyAAC(ov::BitReader &reader, const std::shared_ptr<AudioData> &data);

			MAY_THROWS(BitReaderError)
			std::shared_ptr<AudioData> ProcessExAudioTagHeader(ov::BitReader &reader, SoundFormat sound_format, bool *process_audio_body);

			MAY_THROWS(BitReaderError)
			std::shared_ptr<AudioData> ProcessExAudioTagBody(ov::BitReader &reader, bool process_audio_body, std::shared_ptr<AudioData> audio_data);

		protected:
			AudioPacketType _audio_packet_type;

			std::vector<std::shared_ptr<AudioData>> _data_list;
		};
	}  // namespace flv
}  // namespace modules
