
#include "aac_converter.h"

#include <base/ovlibrary/bit_reader.h>
#include <base/ovlibrary/ovlibrary.h>
#include <modules/bitstream/aac/audio_specific_config.h>

#include "aac_adts.h"

#define OV_LOG_TAG "AACConverter"

std::shared_ptr<ov::Data> AacConverter::MakeAdtsHeader(uint8_t aac_profile, uint8_t aac_sample_rate, uint8_t aac_channels, int16_t data_length, const std::shared_ptr<ov::Data> &data)
{
	uint8_t ADTS_HEADER_LENGTH = 7;
	int16_t aac_frame_length = data_length + 7;

	ov::BitWriter bits(ADTS_HEADER_LENGTH);

	bits.WriteBits(12, 0x0FFF);			   // syncword [12b]
	bits.WriteBits(1, 0);				   // ID - 0=MPEG-4, 1=MPEG-2 [1b]
	bits.WriteBits(2, 0);				   // layer - Always 0 [2b]
	bits.WriteBits(1, 1);				   // protection_absent  [1b]
	bits.WriteBits(2, aac_profile);		   // profile [2b]
	bits.WriteBits(4, aac_sample_rate);	   // sampling_frequency_index[[4b]
	bits.WriteBits(1, 0);				   // private_bit[1b]
	bits.WriteBits(3, aac_channels);	   // channel_configuration[3b]
	bits.WriteBits(1, 0);				   // Original/copy[1b]
	bits.WriteBits(1, 0);				   // Home[1b]
	bits.WriteBits(1, 0);				   // copyright_identification_bit[1b]
	bits.WriteBits(1, 0);				   // copyright_identification_start[1b]
	bits.WriteBits(13, aac_frame_length);  // aac_frame_length[13b]
	bits.WriteBits(11, 0x3F);			   // adts_buffer_fullness[11b]
	bits.WriteBits(2, 0);				   // no_raw_data_blocks_inframe[2b]

	if (data == nullptr)
	{
		return std::make_shared<ov::Data>(bits.GetData(), bits.GetDataSize());
	}

	data->Append(bits.GetData(), bits.GetDataSize());
	return data;
}

/*
	ADTS
	Unlike the ADIF header, ADTS (Audio Data Transport Stream) headers are present before each AAC raw_data_block or block of 2 to 4 raw_data_blocks. Until the MPEG revision from Dec 2002 for MPEG-4 AAC ADTS headers, this was basically the same as a MP3 header, except that the emphasis field was not present for MPEG-2 AAC, only for MPEG-4 AAC.
	Now the emphasis field (2 bits) has been abandoned completely, and thus MPEG-4 and MPEG-2 AAC ADTS headers are exactly the same except for the Object Type ID flag (MPEG-2 or MPEG-4). See also the Wiki page about MP4, because this is important when extracting MPEG-2 AAC files from a MP4 container with mp4creator or creating MP4 files from PsyTEL AAC encodings with this tool.

	The ADTS header has the following fields:
	Field name   Field size in bits   Comment
	ADTS Fixed header: these don't change from frame to frame
	syncword   12   always: '111111111111'
	ID   1   0: MPEG-4, 1: MPEG-2
	layer   2   always: '00'
	protection_absent   1   
	profile   2   
	sampling_frequency_index   4   
	private_bit   1   
	channel_configuration   3   
	original/copy   1   
	home   1   
	ADTS Variable header: these can change from frame to frame
	copyright_identification_bit   1   
	copyright_identification_start   1   
	aac_frame_length   13   length of the frame including header (in bytes)
	adts_buffer_fullness   11   0x7FF indicates VBR
	no_raw_data_blocks_in_frame   2   
	ADTS Error check
	crc_check   16   only if protection_absent == 0
	After that come (no_raw_data_blocks_in_frame+1) raw_data_blocks.
	Some elaborations:
	profile
	bits   ID == 1 (MPEG-2 profile)   ID == 0 (MPEG-4 Object type)
	00 (0)   Main profile   AAC MAIN
	01 (1)   Low Complexity profile (LC)   AAC LC
	10 (2)   Scalable Sample Rate profile (SSR)   AAC SSR
	11 (3)   (reserved)   AAC LTP
*/

// Raw audio data must be 1 frame
std::shared_ptr<ov::Data> AacConverter::ConvertRawToAdts(const uint8_t *data, size_t data_len, const AudioSpecificConfig &aac_config)
{
	auto adts_data = std::make_shared<ov::Data>(data_len + 16);

	MakeAdtsHeader(
		static_cast<uint8_t>(aac_config.GetAacProfile()),
		static_cast<uint8_t>(aac_config.ProbeAacSamplingFrequencyIndex()),
		aac_config.Channel(),
		data_len,
		adts_data);

	adts_data->Append(data, data_len);

	return adts_data;
}

std::shared_ptr<ov::Data> AacConverter::ConvertRawToAdts(const std::shared_ptr<const ov::Data> &data, const std::shared_ptr<AudioSpecificConfig> &aac_config)
{
	if (aac_config == nullptr)
	{
		return nullptr;
	}

	return ConvertRawToAdts(data->GetDataAs<uint8_t>(), data->GetLength(), *aac_config);
}

std::shared_ptr<ov::Data> AacConverter::ConvertRawToAdts(const std::shared_ptr<const ov::Data> &data, const std::shared_ptr<ov::Data> &aac_config_data)
{
	if (aac_config_data == nullptr)
	{
		return nullptr;
	}

	AudioSpecificConfig config;
	if (config.Parse(aac_config_data) == false)
	{
		return nullptr;
	}

	return ConvertRawToAdts(data->GetDataAs<uint8_t>(), data->GetLength(), config);
}

std::shared_ptr<ov::Data> AacConverter::ConvertAdtsToRaw(const std::shared_ptr<const ov::Data> &data, std::vector<size_t> *length_list)
{
	auto raw_data = std::make_shared<ov::Data>(data->GetLength());
	size_t remained = data->GetLength();
	off_t offset = 0L;
	auto buffer = data->GetDataAs<uint8_t>();

	while (remained > 0)
	{
		if (remained < ADTS_MIN_SIZE)
		{
			// data is not ADTS format
			return nullptr;
		}

		// Reference: https://wiki.multimedia.cx/index.php/ADTS
		// AAAAAAAA AAAA B CC D EE FFFF G HHH I J K L MMMMMMMMMMMMM OOOOOOOOOOO PP (QQQQQQQQ QQQQQQQQ)
		// 76543210 7654 3 21 0 76 5432 1 076 5 4 3 2 1076543210765 43210765432 10  76543210 76543210
		// |-  0 -| |-   1   -| |-   2   -||-    3    -||-  4 -||-  5  -||-  6  -|  |-  7 -| |-  8 -|
		// Letter	Length (bits)
		// A		12	syncword 0xFFF, all bits must be 1
		// B		1	MPEG Version: 0 for MPEG-4, 1 for MPEG-2
		// C		2	Layer: always 0
		// D		1	protection absent, Warning, set to 1 if there is no CRC and 0 if there is CRC
		// E		2	profile, the MPEG-4 Audio Object Type minus 1
		// F		4	MPEG-4 Sampling Frequency Index (15 is forbidden)
		// G		1	private bit, guaranteed never to be used by MPEG, set to 0 when encoding, ignore when decoding
		// H		3	MPEG-4 Channel Configuration (in the case of 0, the channel configuration is sent via an inband PCE)
		// I		1	originality, set to 0 when encoding, ignore when decoding
		// J		1	home, set to 0 when encoding, ignore when decoding
		// K		1	copyrighted id bit, the next bit of a centrally registered copyright identifier, set to 0 when encoding, ignore when decoding
		// L		1	copyright id start, signals that this frame's copyright id bit is the first bit of the copyright id, set to 0 when encoding, ignore when decoding
		// M		13	frame length, this value must include 7 or 9 bytes of header length: FrameLength = (ProtectionAbsent == 1 ? 7 : 9) + size(AACFrame)
		// O		11	Buffer fullness
		// P		2	Number of AAC frames (RDBs) in ADTS frame minus 1, for maximum compatibility always use 1 AAC frame per ADTS frame
		// Q		16	CRC if protection absent is 0

		auto second_byte = buffer[1];

		// A - Check syncword
		if ((buffer[0] != 0xFF) || ((second_byte & 0xF0) != 0xF0))
		{
			// data is not ADTS format
			return nullptr;
		}

		// C - Layer
		bool layer = OV_GET_BITS(uint8_t, second_byte, 1, 2);

		if (layer != 0)
		{
			// Layer always 0
			return nullptr;
		}

		// D - Protection absent
		uint8_t protection_absent = OV_GET_BIT(second_byte, 0);

		// M - Frame length
		size_t frame_length =
			OV_GET_BITS(uint8_t, buffer[3], 0, 2) << 11 |
			(buffer[4] << 3) |
			OV_GET_BITS(uint8_t, buffer[5], 5, 3);

		size_t header_length = (protection_absent == 1) ? 7 : 9;

		if ((frame_length < header_length) || (frame_length > remained))
		{
			// Invalid/Not enough data
			return nullptr;
		}

		size_t payload_length = frame_length - header_length;

		// Skip ADTS header
		buffer += header_length;

		raw_data->Append(buffer, payload_length);
		if (length_list != nullptr)
		{
			length_list->push_back(payload_length);
		}

		buffer += payload_length;

		remained -= frame_length;
		offset += frame_length;
	}

	return raw_data;
}

ov::String AacConverter::GetProfileString(const std::shared_ptr<AudioSpecificConfig> &aac_config)
{
	if (aac_config == nullptr)
	{
		return "";
	}

	return ov::String::FormatString("%d", static_cast<int>(aac_config->ObjectType()));
}

ov::String AacConverter::GetProfileString(const std::shared_ptr<ov::Data> &aac_config_data)
{
	if (aac_config_data == nullptr)
	{
		return "";
	}

	AudioSpecificConfig audio_config;
	if (audio_config.Parse(aac_config_data) == false)
	{
		return "";
	}

	return ov::String::FormatString("%d", static_cast<int>(audio_config.ObjectType()));
}