#pragma once

#include <base/ovlibrary/ovlibrary.h>
#include <stdint.h>

#include "audio_specific_config.h"
#include "base/mediarouter/media_buffer.h"

// Default = AacObjectTypeAacLC
#define AAC_CONVERTER_DEFAULT_PROFILE "2"

class AacConverter
{
public:
	static std::shared_ptr<ov::Data> ConvertRawToAdts(const uint8_t *data, size_t data_len, const AudioSpecificConfig &aac_config);
	static std::shared_ptr<ov::Data> ConvertRawToAdts(const std::shared_ptr<const ov::Data> &data, const std::shared_ptr<AudioSpecificConfig> &aac_config);
	static std::shared_ptr<ov::Data> ConvertRawToAdts(const std::shared_ptr<const ov::Data> &data, const std::shared_ptr<ov::Data> &aac_config);
	static std::shared_ptr<ov::Data> ConvertAdtsToRaw(const std::shared_ptr<const ov::Data> &data, std::vector<size_t> *length_list);

	static ov::String GetProfileString(const std::shared_ptr<AudioSpecificConfig> &aac_config);
	static ov::String GetProfileString(const std::shared_ptr<ov::Data> &aac_config_data);

	static std::shared_ptr<ov::Data> MakeAdtsHeader(uint8_t aac_profile, uint8_t aac_sample_rate, uint8_t aac_channels, int16_t data_length, const std::shared_ptr<ov::Data> &data = nullptr);
};
