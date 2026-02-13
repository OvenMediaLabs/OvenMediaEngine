#pragma once

#include <base/info/decoder_configuration_record.h>

#include "h264/h264_decoder_configuration_record.h"
#include "h265/h265_decoder_configuration_record.h"
#include "aac/audio_specific_config.h"

enum class DecoderConfigurationRecordType : uint8_t
{
	UNKNOWN = 0,
	AVCConfigurationRecord,
	HEVCConfigurationRecord,
	AudioSpecificConfig
};

class DecoderConfigurationRecordParser
{
public:
	static std::shared_ptr<DecoderConfigurationRecord> Parse(cmn::MediaCodecId codec_id, const std::shared_ptr<ov::Data> &data)
	{
		DecoderConfigurationRecordType type = DecoderConfigurationRecordType::UNKNOWN;

		switch (codec_id)
		{
		case cmn::MediaCodecId::H264:
			type = DecoderConfigurationRecordType::AVCConfigurationRecord;
			break;
		case cmn::MediaCodecId::H265:
			type = DecoderConfigurationRecordType::HEVCConfigurationRecord;
			break;
		case cmn::MediaCodecId::Aac:
			type = DecoderConfigurationRecordType::AudioSpecificConfig;
			break;
		default:
			return nullptr;
		}

		return DecoderConfigurationRecordParser::Parse(type, data);
	}

	static std::shared_ptr<DecoderConfigurationRecord> Parse(DecoderConfigurationRecordType type, const std::shared_ptr<ov::Data> &data)
	{
		if (data == nullptr)
		{
			return nullptr;
		}

		std::shared_ptr<DecoderConfigurationRecord> decoder_configuration_record = nullptr;

		switch (type)
		{
		case DecoderConfigurationRecordType::AVCConfigurationRecord:
			decoder_configuration_record = std::make_shared<AVCDecoderConfigurationRecord>();
			break;
		case DecoderConfigurationRecordType::HEVCConfigurationRecord:
			decoder_configuration_record = std::make_shared<HEVCDecoderConfigurationRecord>();
			break;
		case DecoderConfigurationRecordType::AudioSpecificConfig:
			decoder_configuration_record = std::make_shared<AudioSpecificConfig>();
			break;
		default:
			return nullptr;
		}

		if (decoder_configuration_record != nullptr)
		{
			if (decoder_configuration_record->Parse(data) == false)
			{
				decoder_configuration_record = nullptr;
			}
		}

		return decoder_configuration_record;
	}
};