#pragma once


// Opus Identification Header.
// https://datatracker.ietf.org/doc/html/draft-ietf-codec-oggopus#section-5.1
//
// 0                   1                   2                   3
// 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |      'O'      |      'p'      |      'u'      |      's'      |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |      'H'      |      'e'      |      'a'      |      'd'      |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |  Version = 1  | Channel Count |           Pre-skip            |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |                     Input Sample Rate (Hz)                    |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |   Output Gain (Q7.8 in dB)    | Mapping Family|               |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+               :
// |                                                               |
// :               Optional Channel Mapping Table...               :
// |                                                               |
//  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+

// opus_default_extradata (ffmpeg example)
// https://github.com/skynav/ffmpeg/blob/master/libavcodec/opus.c
// https://github.com/skynav/ffmpeg/blob/master/libavcodec/opus.h

// https://patchwork.ffmpeg.org/project/ffmpeg/patch/CAB0OVGqiFSF001jQeuBALTMUN61NO23gjGrza-RNPM9uQdq1gw@mail.gmail.com/#45978
// https://www.mail-archive.com/libav-user@ffmpeg.org/msg12487.html


#include <base/ovlibrary/ovlibrary.h>
#include <base/info/decoder_configuration_record.h>

// the first 19 bytes are sufficient to bypass the ffmpeg checks
#define MIN_OPUS_SPECIFIC_CONFIG_SIZE 19


class OpusSpecificConfig : public DecoderConfigurationRecord
{
public:
	OpusSpecificConfig()
	{
	}

	OpusSpecificConfig(uint8_t channels, uint32_t sample_rate) :
		_channels(channels),
		_sample_rate(sample_rate)
	{
	}

	bool IsValid() const override
	{
		return (_header == _header_const) && (_version == 1) && (_channels > 0) && (_sample_rate == 48000);
	}

	bool Parse(const std::shared_ptr<const ov::Data> &data) override
	{
		if (data == nullptr || data->GetLength() < MIN_OPUS_SPECIFIC_CONFIG_SIZE)
		{
			//logte("The data inputed is too small for parsing (%d must be bigger than %d)", data->GetLength(), MIN_OPUS_SPECIFIC_CONFIG_SIZE);
			return false;
		}

		BitReader parser(data->GetDataAs<uint8_t>(), data->GetLength());

		_header = parser.ReadString(8);
		_version = parser.ReadBytes<uint8_t>(false);
		_channels = parser.ReadBytes<uint8_t>(false);
		_pre_skip = parser.ReadBytes<uint16_t>(false);
		_sample_rate = parser.ReadBytes<uint32_t>(false);
		_output_gain = parser.ReadBytes<uint16_t>(false);
		_mapping_family = parser.ReadBytes<uint8_t>(false);

		return true;
	}

	bool Equals(const std::shared_ptr<DecoderConfigurationRecord> &other) override
	{
		if (other == nullptr || other->GetData() == nullptr || GetData() == nullptr)
		{
			return false;
		}

		return other->GetData()->IsEqual(GetData());
	}

	std::shared_ptr<const ov::Data> Serialize() override
	{
		ov::BitWriter bits(MIN_OPUS_SPECIFIC_CONFIG_SIZE);

		bits.WriteData(_header_const.ToData()->GetDataAs<uint8_t>(), _header.GetLength());
		bits.WriteBits(8, _version);
		bits.WriteBits(8, _channels);
		bits.WriteBits(16, _pre_skip);
		bits.WriteBits(32, _sample_rate);
		bits.WriteBits(16, _output_gain);
		bits.WriteBits(8, _mapping_family);

		return std::make_shared<ov::Data>(bits.GetData(), bits.GetDataSize());
	}

	// Helpers
	ov::String GetCodecsParameter() const override
	{
		return "mp4a.ad";
	}

private:
	const ov::String _header_const { "OpusHead" };
	ov::String _header { _header_const };
	uint8_t	_version { 1 };
	uint8_t _channels { 0 };
	uint16_t _pre_skip { 0 };
	uint32_t _sample_rate { 0 };
	uint16_t _output_gain { 0 };
	uint8_t _mapping_family { 0 };
};
