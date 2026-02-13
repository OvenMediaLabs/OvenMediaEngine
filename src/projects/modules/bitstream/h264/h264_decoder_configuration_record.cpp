#include <base/ovlibrary/bit_reader.h>
#include <base/ovlibrary/bit_writer.h>
#include <base/ovlibrary/ovlibrary.h>

#include "h264_decoder_configuration_record.h"

#define OV_LOG_TAG "AVCDecoderConfigurationRecord"

bool AVCDecoderConfigurationRecord::IsValid() const
{
	if (_num_of_sps > 0 && _num_of_pps > 0)
	{
		return true;
	}

	return false;
}

ov::String AVCDecoderConfigurationRecord::GetCodecsParameter() const
{
	// <profile idc><constraint set flags><level idc>
	return ov::String::FormatString("avc1.%02x%02x%02x", ProfileIndication(), Compatibility(), LevelIndication());
}

bool AVCDecoderConfigurationRecord::Parse(const std::shared_ptr<const ov::Data> &data)
{
	if (data == nullptr)
	{
		logte("The data inputted is null");
		return false;
	}

	auto pdata = data->GetDataAs<uint8_t>();
	auto data_length = data->GetLength();

	if (data_length < MIN_AVCDECODERCONFIGURATIONRECORD_SIZE)
	{
		logte("The data inputted is too small for parsing (%d must be bigger than %d)", data_length, MIN_AVCDECODERCONFIGURATIONRECORD_SIZE);
		return false;
	}

	BitReader parser(pdata, data_length);

	SetVersion(parser.ReadBytes<uint8_t>());
	SetProfileIndication(parser.ReadBytes<uint8_t>());
	SetCompatibility(parser.ReadBytes<uint8_t>());
	SetLevelIndication(_level_indication = parser.ReadBytes<uint8_t>());
	_reserved1 = parser.ReadBits<uint8_t>(6);
	// 2022-11-16 Some encoder does not set the _reserved1 to 0b111111
	// if (record._reserved1 != 0b111111)
	// {
	// 	return false;
	// }
	SetLengthMinusOne(parser.ReadBits<uint8_t>(2));
	_reserved2 = parser.ReadBits<uint8_t>(3);
	// 2022-11-16 Some encoder does not set the _reserved2 to 0b111
	// if (record._reserved2 != 0b111)
	// {
	// 	return false;
	// }

	auto num_of_sps = parser.ReadBits<uint8_t>(5);
	for (uint8_t i = 0; i < num_of_sps; i++)
	{
		uint16_t sps_length = parser.ReadBytes<uint16_t>();
		if (sps_length == 0 || parser.BytesRemained() < sps_length)
		{
			return false;
		}
		auto sps = std::make_shared<ov::Data>(parser.CurrentPosition(), sps_length);
		if (AddSPS(sps) == false)
		{
			return false;
		}

		parser.SkipBytes(sps_length);
	}

	auto num_of_pps = parser.ReadBytes<uint8_t>();
	for (uint8_t i = 0; i < num_of_pps; i++)
	{
		uint16_t pps_length = parser.ReadBytes<uint16_t>();
		if (pps_length == 0 || parser.BytesRemained() < pps_length)
		{
			return false;
		}

		auto pps = std::make_shared<ov::Data>(parser.CurrentPosition(), pps_length);
		if (AddPPS(pps) == false)
		{
			return false;
		}

		parser.SkipBytes(pps_length);
	}

	if (_profile_indication == 100 || _profile_indication == 110 ||
		_profile_indication == 122 || _profile_indication == 144)
	{
		_reserved3 = parser.ReadBits<uint8_t>(6);
		SetChromaFormat(parser.ReadBits<uint8_t>(2));
		_reserved4 = parser.ReadBits<uint8_t>(5);
		SetBitDepthLumaMinus8(parser.ReadBits<uint8_t>(3));
		_reserved5 = parser.ReadBits<uint8_t>(5);
		SetBitDepthChromaMinus8(parser.ReadBits<uint8_t>(3));

		auto num_of_sps_ext = parser.ReadBytes<uint8_t>();
		for (uint8_t i = 0; i < num_of_sps_ext; i++)
		{
			uint16_t sps_ext_length = parser.ReadBytes<uint16_t>();
			if (sps_ext_length == 0 || parser.BytesRemained() < sps_ext_length)
			{
				return false;
			}

			auto sps_ext = std::make_shared<ov::Data>(parser.CurrentPosition(), sps_ext_length);
			if (AddSPSExt(sps_ext) == false)
			{
				return false;
			}

			parser.SkipBytes(sps_ext_length);
		}
	}

	// No need to serialize the data
	SetData(data);

	return IsValid();
}

bool AVCDecoderConfigurationRecord::ParseV2Internal(ov::BitReader &reader)
{
	auto data = reader.GetData();

	if (reader.GetRemainingBytes() < MIN_AVCDECODERCONFIGURATIONRECORD_SIZE)
	{
		logte("The data inputted is too small for parsing (%d must be bigger than %d)", reader.GetRemainingBytes(), MIN_AVCDECODERCONFIGURATIONRECORD_SIZE);
		return false;
	}

	SetVersion(reader.ReadU8());
	SetProfileIndication(reader.ReadU8());
	SetCompatibility(reader.ReadU8());
	SetLevelIndication(_level_indication = reader.ReadU8());
	_reserved1 = reader.ReadAs<uint8_t>(6);
	// 2022-11-16 Some encoder does not set the _reserved1 to 0b111111
	// if (record._reserved1 != 0b111111)
	// {
	// 	return false;
	// }
	SetLengthMinusOne(reader.ReadAs<uint8_t>(2));
	_reserved2 = reader.ReadAs<uint8_t>(3);
	// 2022-11-16 Some encoder does not set the _reserved2 to 0b111
	// if (record._reserved2 != 0b111)
	// {
	// 	return false;
	// }

	auto num_of_sps = reader.ReadAs<uint8_t>(5);
	for (uint8_t i = 0; i < num_of_sps; i++)
	{
		uint16_t sps_length = reader.ReadU16BE();
		if (sps_length == 0 || reader.GetRemainingBytes() < sps_length)
		{
			return false;
		}
		auto sps = reader.ReadBytes(sps_length);
		if (AddSPS(sps) == false)
		{
			return false;
		}
	}

	auto num_of_pps = reader.ReadU8();
	for (uint8_t i = 0; i < num_of_pps; i++)
	{
		uint16_t pps_length = reader.ReadU16BE();
		if (pps_length == 0 || reader.GetRemainingBytes() < pps_length)
		{
			return false;
		}

		auto pps = reader.ReadBytes(pps_length);
		if (AddPPS(pps) == false)
		{
			return false;
		}
	}

	if (_profile_indication == 100 || _profile_indication == 110 ||
		_profile_indication == 122 || _profile_indication == 144)
	{
		_reserved3 = reader.ReadAs<uint8_t>(6);
		SetChromaFormat(reader.ReadAs<uint8_t>(2));
		_reserved4 = reader.ReadAs<uint8_t>(5);
		SetBitDepthLumaMinus8(reader.ReadAs<uint8_t>(3));
		_reserved5 = reader.ReadAs<uint8_t>(5);
		SetBitDepthChromaMinus8(reader.ReadAs<uint8_t>(3));

		auto num_of_sps_ext = reader.ReadU8();
		for (uint8_t i = 0; i < num_of_sps_ext; i++)
		{
			uint16_t sps_ext_length = reader.ReadU16BE();
			if (sps_ext_length == 0 || reader.GetRemainingBytes() < sps_ext_length)
			{
				return false;
			}

			auto sps_ext = reader.ReadBytes(sps_ext_length);
			if (AddSPSExt(sps_ext) == false)
			{
				return false;
			}
		}
	}

	// No need to serialize the data
	SetData(data);

	return IsValid();
}

bool AVCDecoderConfigurationRecord::Equals(const std::shared_ptr<DecoderConfigurationRecord> &other)  
{
	if (other == nullptr)
	{
		return false;
	}
	
	auto other_config = std::dynamic_pointer_cast<AVCDecoderConfigurationRecord>(other);
	if (other_config == nullptr)
	{
		return false;
	}

	if (ProfileIndication() != other_config->ProfileIndication())
	{
		return false;
	}

	if (LevelIndication() != other_config->LevelIndication())
	{
		return false;
	}

	if(GetWidth() != other_config->GetWidth())
	{
		return false;
	}

	if(GetHeight() != other_config->GetHeight())
	{
		return false;
	}

	return true;
}

std::shared_ptr<const ov::Data> AVCDecoderConfigurationRecord::Serialize()
{
	ov::BitWriter bits(512);

	bits.WriteBits(8, Version());
	bits.WriteBits(8, ProfileIndication());
	bits.WriteBits(8, Compatibility());
	bits.WriteBits(8, LevelIndication());
	bits.WriteBits(6, 0x3F);  // 111111'b
	bits.WriteBits(2, LengthMinusOne());
	bits.WriteBits(3, 0x07);		// 111b

	bits.WriteBits(5, NumOfSPS());	// num of SPS
	for (auto i = 0; i < NumOfSPS(); i++)
	{
		auto sps = GetSPSData(i);
		bits.WriteBits(16, sps->GetLength());  // sps length
		bits.WriteData(sps->GetDataAs<uint8_t>(), sps->GetLength());
	}

	bits.WriteBits(8, NumOfPPS());	// num of PPS
	for (auto i = 0; i < NumOfPPS(); i++)
	{
		auto pps = GetPPSData(i);
		bits.WriteBits(16, pps->GetLength());  // pps length
		bits.WriteData(pps->GetDataAs<uint8_t>(), pps->GetLength());
	}

	return bits.GetDataObject();
}

uint8_t AVCDecoderConfigurationRecord::Version() const
{
	return _version;
}

uint8_t AVCDecoderConfigurationRecord::ProfileIndication() const
{
	return _profile_indication;
}

uint8_t AVCDecoderConfigurationRecord::Compatibility() const
{
	return _profile_compatibility;
}

uint8_t AVCDecoderConfigurationRecord::LevelIndication() const
{
	return _level_indication;
}

uint8_t AVCDecoderConfigurationRecord::LengthMinusOne() const
{
	return _length_minus_one;
}
 
uint8_t AVCDecoderConfigurationRecord::NumOfSPS() const
{
	return _num_of_sps;
}

uint8_t AVCDecoderConfigurationRecord::NumOfPPS() const
{
	return _num_of_pps;
}

uint8_t AVCDecoderConfigurationRecord::NumOfSPSExt() const
{
	return _num_of_sps_ext;
}

std::shared_ptr<const ov::Data> AVCDecoderConfigurationRecord::GetSPSData(int index) const
{
	if (static_cast<int>(_sps_data_list.size()) - 1 < index)
	{
		return nullptr;
	}

	return _sps_data_list[index];
}

std::shared_ptr<const ov::Data> AVCDecoderConfigurationRecord::GetPPSData(int index) const
{
	if (static_cast<int>(_pps_data_list.size()) - 1 < index)
	{
		return nullptr;
	}

	return _pps_data_list[index];
}

std::shared_ptr<const ov::Data> AVCDecoderConfigurationRecord::GetSPSExtData(int index) const
{
	if (static_cast<int>(_sps_ext_data_list.size()) - 1 < index)
	{
		return nullptr;
	}

	return _sps_ext_data_list[index];
}

// Get SPS
bool AVCDecoderConfigurationRecord::GetSPS(int sps_id, H264SPS &sps) const
{
	auto it = _sps_map.find(sps_id);
    if (it == _sps_map.end())
    {
        return false;
    }

    sps = it->second;

    return true;
}

bool AVCDecoderConfigurationRecord::GetPPS(int pps_id, H264PPS &pps) const
{
	auto it = _pps_map.find(pps_id);
	if (it == _pps_map.end())
	{
		return false;
	}

	pps = it->second;

	return true;
}

uint8_t AVCDecoderConfigurationRecord::ChromaFormat() const
{
	return _chroma_format;
}

uint8_t AVCDecoderConfigurationRecord::BitDepthLumaMinus8() const
{
	return _bit_depth_luma_minus8;
}

uint8_t AVCDecoderConfigurationRecord::BitDepthChromaMinus8() const
{
	return _bit_depth_chroma_minus8;
}
/* ISO 14496-15, 5.2.4.1
aligned(8) class AVCDecoderConfigurationRecord {
	unsigned int(8) configurationVersion = 1;
	unsigned int(8) AVCProfileIndication;
	unsigned int(8) profile_compatibility;
	unsigned int(8) AVCLevelIndication;
	bit(6) reserved = ‘111111’b;
	unsigned int(2) lengthSizeMinusOne;
	bit(3) reserved = ‘111’b;
	unsigned int(5) numOfSequenceParameterSets;
	for (i=0; i< numOfSequenceParameterSets;  i++) {
		unsigned int(16) sequenceParameterSetLength ;
		bit(8*sequenceParameterSetLength) sequenceParameterSetNALUnit;
	}
	unsigned int(8) numOfPictureParameterSets;
	for (i=0; i< numOfPictureParameterSets;  i++) {
		unsigned int(16) pictureParameterSetLength;
		bit(8*pictureParameterSetLength) pictureParameterSetNALUnit;
	}
	if( profile_idc  ==  100  ||  profile_idc  ==  110  ||
	    profile_idc  ==  122  ||  profile_idc  ==  144 )
	{
		bit(6) reserved = ‘111111’b;
		unsigned int(2) chroma_format;
		bit(5) reserved = ‘11111’b;
		unsigned int(3) bit_depth_luma_minus8;
		bit(5) reserved = ‘11111’b;
		unsigned int(3) bit_depth_chroma_minus8;
		unsigned int(8) numOfSequenceParameterSetExt;
		for (i=0; i< numOfSequenceParameterSetExt; i++) {
			unsigned int(16) sequenceParameterSetExtLength;
			bit(8*sequenceParameterSetExtLength) sequenceParameterSetExtNALUnit;
		}
	}
} */

std::tuple<std::shared_ptr<ov::Data>, FragmentationHeader> AVCDecoderConfigurationRecord::GetSpsPpsAsAnnexB(uint8_t start_code_size, bool need_aud)
{
	if (IsValid() == false)
	{
		return {nullptr, {}};
	}

	if (_sps_pps_annexb_data != nullptr)
	{
		return {_sps_pps_annexb_data, _sps_pps_annexb_frag_header};
	}

	auto data = std::make_shared<ov::Data>(1024);
	FragmentationHeader frag_header;
	size_t offset = 0;

	uint8_t START_CODE[4];

	START_CODE[0] = 0x00;
	START_CODE[1] = 0x00;
	if (start_code_size == 3)
	{
		START_CODE[2] = 0x01;
	}
	else  // 4
	{
		START_CODE[2] = 0x00;
		START_CODE[3] = 0x01;
	}

	if (need_aud)
	{
		static uint8_t AUD[2] = {0x09, 0xf0};
		// AUD
		data->Append(START_CODE, start_code_size);
		offset += start_code_size;

		frag_header.fragmentation_offset.push_back(offset);
		frag_header.fragmentation_length.push_back(sizeof(AUD));

		offset += sizeof(AUD);

		data->Append(AUD, sizeof(AUD));
	}

	for (int i = 0; i < NumOfSPS(); i++)
	{
		data->Append(START_CODE, start_code_size);
		offset += start_code_size;

		auto sps = GetSPSData(i);
		frag_header.fragmentation_offset.push_back(offset);
		frag_header.fragmentation_length.push_back(sps->GetLength());

		offset += sps->GetLength();

		data->Append(sps);
	}

	for (int i = 0; i < NumOfPPS(); i++)
	{
		data->Append(START_CODE, start_code_size);
		offset += start_code_size;

		auto pps = GetPPSData(i);
		frag_header.fragmentation_offset.push_back(offset);
		frag_header.fragmentation_length.push_back(pps->GetLength());

		offset += pps->GetLength();

		data->Append(pps);
	}

	_sps_pps_annexb_data = data;
	_sps_pps_annexb_frag_header = frag_header;

	return {_sps_pps_annexb_data, _sps_pps_annexb_frag_header};
}

void AVCDecoderConfigurationRecord::SetVersion(uint8_t version)
{
	_version = version;
}

void AVCDecoderConfigurationRecord::SetProfileIndication(uint8_t profile_indication)
{
	_profile_indication = profile_indication;
}

void AVCDecoderConfigurationRecord::SetCompatibility(uint8_t profile_compatibility)
{
	_profile_compatibility = profile_compatibility;
}

void AVCDecoderConfigurationRecord::SetLevelIndication(uint8_t level_indication)
{
	_level_indication = level_indication;
}

void AVCDecoderConfigurationRecord::SetLengthMinusOne(uint8_t length_minus_one)
{
	_length_minus_one = length_minus_one;
}

void AVCDecoderConfigurationRecord::SetChromaFormat(uint8_t chroma_format)
{
	_chroma_format = chroma_format;
}

void AVCDecoderConfigurationRecord::SetBitDepthLumaMinus8(uint8_t bit_depth_luma_minus8)
{
	_bit_depth_luma_minus8 = bit_depth_luma_minus8;
}

void AVCDecoderConfigurationRecord::SetBitDepthChromaMinus8(uint8_t bit_depth_chroma_minus8)
{
	_bit_depth_chroma_minus8 = bit_depth_chroma_minus8;
}

bool AVCDecoderConfigurationRecord::AddSPS(const std::shared_ptr<const ov::Data> &nalu)
{
	H264SPS sps;
	if (H264Parser::ParseSPS(nalu->GetDataAs<uint8_t>(), nalu->GetLength(), sps) == false)
	{
		logte("Could not parse H264 SPS unit");
		return false;
	}

	if (_sps_map.find(sps.GetId()) != _sps_map.end())
	{
		return false;
	}

	_h264_sps = sps; // last sps
	SetProfileIndication(sps.GetProfileIdc());
	SetCompatibility(sps.GetConstraintFlag());
	SetLevelIndication(sps.GetCodecLevelIdc());

	_sps_data_list.push_back(nalu);
	_sps_map.emplace(sps.GetId(), sps);

	_num_of_sps++;

	return true;
}

bool AVCDecoderConfigurationRecord::AddPPS(const std::shared_ptr<const ov::Data> &nalu)
{
	H264PPS pps;
	if (H264Parser::ParsePPS(nalu->GetDataAs<uint8_t>(), nalu->GetLength(), pps) == false)
	{
		logte("Could not parse H264 PPS unit");
		return false;
	}

	if (_pps_map.find(pps.GetId()) != _pps_map.end())
	{
		return false;
	}

	_pps_map.emplace(pps.GetId(), pps);
	_pps_data_list.push_back(nalu);
	_num_of_pps++;

	return true;
}

bool AVCDecoderConfigurationRecord::AddSPSExt(const std::shared_ptr<const ov::Data> &sps_ext)
{
	_sps_ext_data_list.push_back(sps_ext);
	_num_of_sps_ext++;

	return true;
}

int32_t AVCDecoderConfigurationRecord::GetWidth() const
{
	return _h264_sps.GetWidth();
}

int32_t AVCDecoderConfigurationRecord::GetHeight() const
{
	return _h264_sps.GetHeight();
}

ov::String AVCDecoderConfigurationRecord::GetInfoString() const
{
	ov::String out_str = ov::String::FormatString("\n[AVCDecoderConfigurationRecord]\n");

	out_str.AppendFormat("\tVersion(%d)\n", Version());
	out_str.AppendFormat("\tProfileIndication(%d)\n", ProfileIndication());
	out_str.AppendFormat("\tCompatibility(%d)\n", Compatibility());
	out_str.AppendFormat("\tLevelIndication(%d)\n", LevelIndication());
	out_str.AppendFormat("\tLengthOfNALUnit(%d)\n", LengthMinusOne());
	out_str.AppendFormat("\tWidth(%d)\n", GetWidth());
	out_str.AppendFormat("\tHeight(%d)\n", GetHeight());
	out_str.AppendFormat("\tNumOfSPS(%d)\n", NumOfSPS());
	out_str.AppendFormat("\tNumOfPPS(%d)\n", NumOfPPS());
	out_str.AppendFormat("\tNumOfSPSExt(%d)\n", NumOfSPSExt());
	out_str.AppendFormat("\tChromaFormat(%d)\n", ChromaFormat());
	out_str.AppendFormat("\tBitDepthLumaMinus8(%d)\n", BitDepthLumaMinus8());
	out_str.AppendFormat("\tBitDepthChromaMinus8(%d)\n", BitDepthChromaMinus8());

	return out_str;
}
