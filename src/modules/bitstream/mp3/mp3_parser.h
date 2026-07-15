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

	// Total frame size in bytes (header + payload). Returns 0 if it cannot be determined.
	// Use it to find where the next frame starts.
	uint32_t GetFrameLength();

	double GetVersion();
	uint8_t GetLayer();

	ov::String GetInfoString();
	
private:
	// Default-initialized so the getters never read indeterminate values when
	// Parse() fails early (e.g. syncword mismatch) or the object is queried
	// without a successful Parse().
	uint8_t _version_id = 0;
	uint8_t _layer_id = 0;
	uint8_t _protection_bit = 0;
	uint8_t _bitrate_index = 0;
	uint8_t _sampling_frequency = 0;
	uint8_t _padding_bit = 0;
	uint8_t _private_bit = 0;
	uint8_t _channel_mode = 0;
	uint8_t _mode_extension = 0;
	uint8_t _copy_right = 0;
	uint8_t _original = 0;
	uint8_t _emphasis = 0;

	uint32_t _bitrate = 0;
	uint32_t _sampling_rate = 0;
	uint8_t _channel_count = 0;
};