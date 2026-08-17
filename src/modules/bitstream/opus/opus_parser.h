#pragma once

#include <base/ovlibrary/ovlibrary.h>

#include <stdint.h>

class OPUSParser
{

public:
	// static bool AppendAdtsHeader(AacObjectType profile, SamplingFrequencies samplerate, int32_t channels, std::shared_ptr<ov::Data> &media_packet_data);
	static bool IsValid(const uint8_t *data, size_t data_length);
	static bool Parse(const uint8_t *data, size_t data_length, OPUSParser &parser);

	uint8_t GetStereoFlag();
	
	ov::String GetInfoString();

	uint8_t _configuration_number;
	uint8_t _stereo_flag;
	uint8_t _frame_count_code;
};