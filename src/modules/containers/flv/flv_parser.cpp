//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2020 AirenSoft. All rights reserved.
//
//==============================================================================
#include "flv_parser.h"

#include <base/ovlibrary/bit_reader.h>
#include <base/ovlibrary/ovlibrary.h>

#define OV_LOG_TAG "FlvParser"

namespace flv
{
	bool VideoData::Parse(const std::shared_ptr<const ov::Data> &data)
	{
		if (data->GetLength() < MIN_VIDEO_DATA_LENGTH)
		{
			logte("The data inputted is too small for parsing (%zu must be bigger than %d)", data->GetLength(), MIN_VIDEO_DATA_LENGTH);
			return false;
		}

		BitReader parser(data);

		_frame_type = static_cast<VideoFrameType>(parser.ReadBits<uint8_t>(4));
		_codec_id = static_cast<VideoCodecId>(parser.ReadBits<uint8_t>(4));

		if (_codec_id != VideoCodecId::AVC)
		{
			logte("Unsupported codec : %d", static_cast<uint8_t>(_codec_id));
			return false;
		}

		_packet_type = static_cast<AvcPacketType>(parser.ReadBytes<uint8_t>());

		int32_t composition_time = static_cast<int32_t>(parser.ReadBits<uint32_t>(24));

		// Need to convert UI24 to SI24
		_composition_time = OV_CHECK_FLAG(composition_time, 0x800000) ? composition_time |= 0xFF000000 : composition_time;
		_payload = parser.CurrentPosition();
		_payload_length = parser.BytesRemained();

		return true;
	}

	VideoFrameType VideoData::FrameType()
	{
		return _frame_type;
	}

	VideoCodecId VideoData::CodecId()
	{
		return _codec_id;
	}

	AvcPacketType VideoData::PacketType()
	{
		return _packet_type;
	}

	int64_t VideoData::CompositionTime()
	{
		return _composition_time;
	}

	const uint8_t *VideoData::Payload()
	{
		return _payload;
	}
	size_t VideoData::PayloadLength()
	{
		return _payload_length;
	}

	bool AudioData::Parse(const std::shared_ptr<const ov::Data> &data)
	{
		if (data->GetLength() < MIN_AUDIO_DATA_LENGTH)
		{
			logte("The data inputted is too small for parsing (%zu must be bigger than %d)", data->GetLength(), MIN_AUDIO_DATA_LENGTH);
			return false;
		}

		BitReader parser(data);

		_format = static_cast<SoundFormat>(parser.ReadBits<uint8_t>(4));
		_sample_rate = static_cast<SoundRate>(parser.ReadBits<uint8_t>(2));
		_sample_size = static_cast<SoundSize>(parser.ReadBit());
		_channel = static_cast<SoundType>(parser.ReadBit());
		_packet_type = static_cast<AACPacketType>(parser.ReadBytes<uint8_t>());

		_payload = parser.CurrentPosition();
		_payload_length = parser.BytesRemained();

		return true;
	}

	SoundFormat AudioData::Format()
	{
		return _format;
	}

	SoundRate AudioData::SampleRate()
	{
		return _sample_rate;
	}

	SoundSize AudioData::SampleSize()
	{
		return _sample_size;
	}

	SoundType AudioData::Channel()
	{
		return _channel;
	}

	AACPacketType AudioData::PacketType()
	{
		return _packet_type;
	}

	const uint8_t *AudioData::Payload()
	{
		return _payload;
	}

	size_t AudioData::PayloadLength()
	{
		return _payload_length;
	}
}  // namespace flv
