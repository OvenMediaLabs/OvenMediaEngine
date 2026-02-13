//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2023 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <base/ovlibrary/ovlibrary.h>

#include <stdint.h>

class MP3Parser
{
public:
	static bool IsValid(const uint8_t *data, size_t data_length);
	static bool Parse(const uint8_t *data, size_t data_length, MP3Parser &parser);

	uint32_t GetBitrate();
	uint32_t GetSampleRate();
	uint8_t GetChannelCount();

	double GetVersion();
	uint8_t GetLayer();

	ov::String GetInfoString();
	
private:
	uint8_t _version_id;
	uint8_t _layer_id;
	uint8_t _protection_bit;
	uint8_t _bitrate_index;
	uint8_t _sampling_frequency;
	uint8_t _padding_bit;
	uint8_t _private_bit;
	uint8_t _channel_mode;
	uint8_t _mode_extension;
	uint8_t _copy_right;
	uint8_t _original;
	uint8_t _emphasis;

	uint32_t _bitrate = 0;
	uint32_t _sampling_rate = 0;
	uint8_t _channel_count = 0;
};