#pragma once

#include "audio_specific_config.h"
#include <base/ovlibrary/ovlibrary.h>

#include <stdint.h>

#define ADTS_MIN_SIZE	7

class AACAdts
{

public:
	// static bool AppendAdtsHeader(AacObjectType profile, SamplingFrequencies samplerate, int32_t channels, std::shared_ptr<ov::Data> &media_packet_data);
	static bool IsValid(const uint8_t *data, size_t data_length);
	static bool Parse(const uint8_t *data, size_t data_length, AACAdts &atds);

	uint8_t Id();
	uint8_t Layer();
	bool ProtectionAbsent();
	AudioObjectType ObjectType();
	AacSamplingFrequencies SamplingFrequencyIndex();
	uint32_t Samplerate();
	uint8_t ChannelConfiguration();
	bool Originality();
	bool Home();
	uint16_t AacFrameLength();

	ov::String GetInfoString();

private:
	uint16_t _syncword; // 12 bits (0xFFF)
	uint8_t _id = 0; // 1 bit (0: MPEG4 1: MPEG2)
	uint8_t _layer = 0; // 2 bits (always 0)
	bool _protection_absent; // 1 bit (1: no CRC | 0: CRC)
	uint8_t _profile; // 2 bits (AacObjectType - 1)
	AacSamplingFrequencies _sampling_frequency_index; // 4 bits (15 is forbidden)
	uint8_t _private_bit; // 1 bit (never to be used by MPEG, set 0: encoding ignore when decoding)
	uint8_t _channel_configuration; // 3 bits (0 : sent via an inband PCE)
	bool _original_copy; // 1 bit (set 0: encoding, ignore when decoding)
	bool _home; // 1 bit (0: encoding, 1: decoding)

	bool _copyright_identification_bit; // 1 bit
	bool _copyright_identification_start; // 1 bit
	uint16_t _aac_frame_length; // 13 bits
	uint16_t _adts_buffer_fullness; // 11 bits
	uint8_t _number_of_raw_data_blocks_in_frame; // 2 bits
	uint16_t _crc; // 16 bit
};