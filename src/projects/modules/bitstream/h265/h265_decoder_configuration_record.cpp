#include <base/ovlibrary/ovlibrary.h>
#include <base/common_types.h>
#include "h265_decoder_configuration_record.h"

#define OV_LOG_TAG "HEVCDecoderConfigurationRecord"

bool HEVCDecoderConfigurationRecord::IsValid() const
{
	// VPS, SPS, PPS are mandatory
	if (_vps_data_list.empty() ||
		_sps_data_list.empty() ||
		_pps_data_list.empty())
	{
		return false;
	}
	
	return true;
}

int32_t HEVCDecoderConfigurationRecord::GetWidth()
{
	if (IsValid() == false)
	{
		return 0;
	}

	return _h265_sps.GetWidth();
}

int32_t HEVCDecoderConfigurationRecord::GetHeight()
{
	if (IsValid() == false)
	{
		return 0;
	}

	return _h265_sps.GetHeight();
}

ov::String HEVCDecoderConfigurationRecord::GetCodecsParameter() const
{
	// ISO 14496-15, Annex E.3
	// When the first element of a value is a code indicating a codec from the High Efficiency Video Coding specification (ISO/IEC 23008-2), as documented in clause 8 (such as 'hev1', 'hev2', 'hvc1', 'hvc2', 'shv1' or 'shc1'), the elements following are a series of values from the HEVC or SHVC decoder configuration record, separated by period characters (“.”). In all numeric encodings, leading zeroes may be omitted,

	// •	the general_profile_space, encoded as no character (general_profile_space == 0), or ‘A’, ‘B’, ‘C’ for general_profile_space 1, 2, 3, followed by the general_profile_idc encoded as a decimal number;
	
	// •	the general_profile_compatibility_flags, encoded in hexadecimal (leading zeroes may be omitted);
	
	// •	the general_tier_flag, encoded as ‘L’ (general_tier_flag==0) or ‘H’ (general_tier_flag==1), followed by the general_level_idc, encoded as a decimal number;
	
	// •	each of the 6 bytes of the constraint flags, starting from the byte containing the general_progressive_source_flag, each encoded as a hexadecimal number, and the encoding of each byte separated by a period; trailing bytes that are zero may be omitted.
	
	// Examples:
	// codecs=hev1.1.2.L93.B0
	// a progressive, non-packed stream, Main Profile, Main Tier, Level 3.1. (Only one byte of flags is given here).
	// codecs=hev1.A4.41.H120.B0.23
	// a (mythical) progressive, non-packed stream in profile space 1, with general_profile_idc 4, some compatibility flags set, and in High tier at Level 4 and two bytes of constraint flags supplied.

	// hev1.1.40000000.L120

	ov::String codecs_parameter;

	codecs_parameter += "hev1";

	if (_general_profile_space == 0)
	{
		codecs_parameter += ov::String::FormatString(".%d", _general_profile_idc);
	}
	else
	{
		codecs_parameter += ov::String::FormatString(".%c%d", 'A' + _general_profile_space - 1, _general_profile_idc);
	}

	// general_profile_compatibility_flags in in reverse order
	uint32_t reverse_value = 0;
	for (uint32_t i=0; i < 32; i++)
	{
		reverse_value <<= 1;
		reverse_value |= (_general_profile_compatibility_flags >> i) & 0x01;
	}

	codecs_parameter += ov::String::FormatString(".%x", reverse_value);
	codecs_parameter += ov::String::FormatString(".%c%d", _general_tier_flag == 0 ? 'L' : 'H', _general_level_idc);

	for (size_t i = 0; i < 6; ++i)
	{
		uint8_t flag = 0;
		// _general_constraint_indicator_flags has 48 bits (6 bytes) values
		// Get first 8 bits (1 byte) from _general_constraint_indicator_flags
		flag = (_general_constraint_indicator_flags >> (8 * (5 - i))) & 0xFF;

		if (flag == 0)
		{
			// Skip trailing zero bytes
			break;
		}

		codecs_parameter += ov::String::FormatString(".%x", flag);
	}

	return codecs_parameter;
}

bool HEVCDecoderConfigurationRecord::Parse(const std::shared_ptr<const ov::Data> &data)
{
	if (data == nullptr)
	{
		return false;
	}

	// check length
	if (data->GetLength() < MIN_HEVCDECODERCONFIGURATIONRECORD_SIZE)
	{
		return false;
	}

	auto pdata = data->GetDataAs<uint8_t>();
	auto data_length = data->GetLength();

	BitReader parser(pdata, data_length);

	// configurationVersion
	_version = parser.ReadBytes<uint8_t>();

	// general_profile_space
	_general_profile_space = parser.ReadBits<uint8_t>(2);

	// general_tier_flag
	_general_tier_flag = parser.ReadBits<uint8_t>(1);

	// general_profile_idc
	_general_profile_idc = parser.ReadBits<uint8_t>(5);

	// general_profile_compatibility_flags
	_general_profile_compatibility_flags = parser.ReadBytes<uint32_t>();

	// general_constraint_indicator_flags
	uint32_t general_constraint_indicator_flags_high = parser.ReadBytes<uint32_t>();
	uint16_t general_constraint_indicator_flags_low = parser.ReadBytes<uint16_t>();

	_general_constraint_indicator_flags = (static_cast<uint64_t>(general_constraint_indicator_flags_high) << 16) | general_constraint_indicator_flags_low;

	// general_level_idc
	_general_level_idc = parser.ReadBytes<uint8_t>();

	// reserved
	parser.ReadBits<uint8_t>(4);

	// min_spatial_segmentation_idc
	_min_spatial_segmentation_idc = parser.ReadBits<uint16_t>(12);

	// reserved
	parser.ReadBits<uint8_t>(6);

	// parallelismType
	_parallelism_type = parser.ReadBits<uint8_t>(2);

	// reserved
	parser.ReadBits<uint8_t>(6);

	// chromaFormat
	_chroma_format = parser.ReadBits<uint8_t>(2);

	// reserved
	parser.ReadBits<uint8_t>(5);

	// bitDepthLumaMinus8
	_bit_depth_luma_minus8 = parser.ReadBits<uint8_t>(3);

	// reserved
	parser.ReadBits<uint8_t>(5);

	// bitDepthChromaMinus8
	_bit_depth_chroma_minus8 = parser.ReadBits<uint8_t>(3);

	// avgFrameRate
	_avg_frame_rate = parser.ReadBytes<uint16_t>();

	// constantFrameRate
	_constant_frame_rate = parser.ReadBits<uint8_t>(2);

	// numTemporalLayers
	_num_temporal_layers = parser.ReadBits<uint8_t>(3);

	// temporalIdNested
	_temporal_id_nested = parser.ReadBits<uint8_t>(1);

	// lengthSizeMinusOne
	_length_size_minus_one = parser.ReadBits<uint8_t>(2);

	// numOfArrays
	auto num_of_arrays = parser.ReadBits<uint8_t>(8);

	// check length
	// if (parser.BytesRemained() < num_of_arrays * 3)
	// {
	// 	return false;
	// }

	for (size_t i = 0; i < num_of_arrays; ++i)
	{
		// array_completeness
		[[maybe_unused]] auto array_completeness = parser.ReadBits<uint8_t>(1);

		// reserved
		parser.ReadBits<uint8_t>(1);

		// NAL_unit_type
		auto nal_unit_type = parser.ReadBits<uint8_t>(6);

		// numNalus
		auto num_nalus = parser.ReadBytes<uint16_t>();

		// check length
		if (parser.BytesRemained() < num_nalus)
		{
			return false;
		}

		for (size_t j = 0; j < num_nalus; ++j)
		{
			// nalUnitLength
			auto nal_unit_length = parser.ReadBytes<uint16_t>();

			// check length
			if (parser.BytesRemained() < nal_unit_length)
			{
				return false;
			}

			// nalUnit
			auto nal_unit = std::make_shared<ov::Data>(parser.CurrentPosition(), nal_unit_length);

			// add nalUnit to _nal_units
			// auto &v = _nal_units[nal_unit_type];
			// v.push_back(nal_unit);
			AddNalUnit(static_cast<H265NALUnitType>(nal_unit_type), nal_unit);

			// skip nalUnit
			parser.SkipBytes(nal_unit_length);
		}
	}

	return true;
}

bool HEVCDecoderConfigurationRecord::ParseV2Internal(ov::BitReader &reader)
{
	// check length
	if (reader.GetRemainingBytes() < MIN_HEVCDECODERCONFIGURATIONRECORD_SIZE)
	{
		return false;
	}

	// configurationVersion
	_version = reader.ReadU8();

	// general_profile_space
	_general_profile_space = reader.ReadAs<uint8_t>(2);

	// general_tier_flag
	_general_tier_flag = reader.ReadAs<uint8_t>(1);

	// general_profile_idc
	_general_profile_idc = reader.ReadAs<uint8_t>(5);

	// general_profile_compatibility_flags
	_general_profile_compatibility_flags = reader.ReadU32BE();

	// general_constraint_indicator_flags
	{
		auto high = reader.ReadU32BE();
		auto low = reader.ReadU16BE();

		_general_constraint_indicator_flags = (static_cast<uint64_t>(high) << 16) | low;
	}

	// general_level_idc
	_general_level_idc = reader.ReadU8();

	// reserved
	reader.SkipBits(4);

	// min_spatial_segmentation_idc
	_min_spatial_segmentation_idc = reader.ReadAs<uint16_t>(12);

	// reserved
	reader.SkipBits(6);

	// parallelismType
	_parallelism_type = reader.ReadAs<uint8_t>(2);

	// reserved
	reader.SkipBits(6);

	// chromaFormat
	_chroma_format = reader.ReadAs<uint8_t>(2);

	// reserved
	reader.SkipBits(5);

	// bitDepthLumaMinus8
	_bit_depth_luma_minus8 = reader.ReadAs<uint8_t>(3);

	// reserved
	reader.SkipBits(5);

	// bitDepthChromaMinus8
	_bit_depth_chroma_minus8 = reader.ReadAs<uint8_t>(3);

	// avgFrameRate
	_avg_frame_rate = reader.ReadU16BE();

	// constantFrameRate
	_constant_frame_rate = reader.ReadAs<uint8_t>(2);

	// numTemporalLayers
	_num_temporal_layers = reader.ReadAs<uint8_t>(3);

	// temporalIdNested
	_temporal_id_nested = reader.ReadAs<uint8_t>(1);

	// lengthSizeMinusOne
	_length_size_minus_one = reader.ReadAs<uint8_t>(2);

	// numOfArrays
	auto num_of_arrays = reader.ReadU8();

	for (size_t i = 0; i < num_of_arrays; ++i)
	{
		// array_completeness
		[[maybe_unused]] auto array_completeness = reader.ReadAs<uint8_t>(1);

		// reserved
		reader.SkipBits(1);

		// NAL_unit_type
		auto nal_unit_type = reader.ReadAs<uint8_t>(6);

		// numNalus
		auto num_nalus = reader.ReadU16BE();

		for (size_t j = 0; j < num_nalus; ++j)
		{
			// nalUnitLength
			auto nal_unit_length = reader.ReadU16BE();

			// nalUnit
			auto nalu_data = reader.ReadBytes(nal_unit_length);
			auto nalu_type = static_cast<H265NALUnitType>(nal_unit_type);

			// add nalUnit to _nal_units
			logtt("NALU found: nal_unit_type: %s(%d), length: %d",
				  EnumToString(nalu_type), nalu_type,
				  nal_unit_length);

			AddNalUnit(nalu_type, nalu_data->Clone());
		}
	}

	return true;
}

bool HEVCDecoderConfigurationRecord::Equals(const std::shared_ptr<DecoderConfigurationRecord> &other) 
{
	if (other == nullptr)
	{
		return false;
	}
	
	auto other_config = std::dynamic_pointer_cast<HEVCDecoderConfigurationRecord>(other);
	if (other_config == nullptr)
	{
		return false;
	}

	if (GeneralProfileIDC() != other_config->GeneralProfileIDC())
	{
		return false;
	}

	if (GeneralLevelIDC() != other_config->GeneralLevelIDC())
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

std::shared_ptr<const ov::Data> HEVCDecoderConfigurationRecord::Serialize()
{
	if (IsValid() == false)
	{
		// VPS, SPS, PPS are mandatory
		return nullptr;
	}

	ov::BitWriter bit(1024);

	bit.WriteBits(8, Version()); // configurationVersion
	bit.WriteBits(2, GeneralProfileSpace());
	bit.WriteBits(1, GeneralTierFlag());
	bit.WriteBits(5, GeneralProfileIDC());
	bit.WriteBits(32, GeneralProfileCompatibilityFlags());

	bit.WriteBits(32, GeneralConstraintIndicatorFlags() >> 16);
	bit.WriteBits(16, GeneralConstraintIndicatorFlags() & 0xFFFF);

	bit.WriteBits(8, GeneralLevelIDC());

	bit.WriteBits(4, 0b1111); // reserved
	bit.WriteBits(12, MinSpatialSegmentationIDC());
	bit.WriteBits(6, 0b111111); // reserved
	bit.WriteBits(2, ParallelismType());
	bit.WriteBits(6, 0b111111); // reserved
	bit.WriteBits(2, ChromaFormat());
	bit.WriteBits(5, 0b11111); // reserved
	bit.WriteBits(3, BitDepthLumaMinus8());
	bit.WriteBits(5, 0b11111); // reserved
	bit.WriteBits(3, BitDepthChromaMinus8());
	bit.WriteBits(16, AvgFrameRate());
	bit.WriteBits(2, ConstantFrameRate());
	bit.WriteBits(3, NumTemporalLayers());
	bit.WriteBits(1, TemporalIdNested());
	bit.WriteBits(2, LengthSizeMinusOne());

	bit.WriteBits(8, _nal_units.size()); // numOfArrays

	for (const auto &[nal_type, nal_units] : _nal_units)
	{
		// array_completeness when equal to 1 indicates that all NAL units of the given type are in the following array and none are in the stream; when equal to 0 indicates that additional NAL units of the indicated type may be in the stream; the default and permitted values are constrained by the sample entry name;
		bit.WriteBits(1, 1); // array_completeness

		bit.WriteBits(1, 0); // reserved
		bit.WriteBits(6, nal_type); // nal_unit_type
		bit.WriteBits(16, nal_units.size()); // numNalus

		for (auto &nal_unit : nal_units)
		{
			bit.WriteBits(16, nal_unit->GetLength()); // nalUnitLength
			if (bit.WriteData(nal_unit->GetDataAs<uint8_t>(), nal_unit->GetLength()) == false) // nalUnit
			{
				return nullptr;
			}
		}
	}

	return bit.GetDataObject();
}

void HEVCDecoderConfigurationRecord::AddNalUnit(H265NALUnitType nal_type, const std::shared_ptr<ov::Data> &nal_unit)
{
	auto &v = _nal_units[static_cast<uint8_t>(nal_type)];
	v.push_back(nal_unit);
	
	if (nal_type == H265NALUnitType::VPS)
	{
		AddVPS(nal_unit);
	}
	else if (nal_type == H265NALUnitType::SPS)
	{
		// Set Info from SPS
		AddSPS(nal_unit);
	}
	else if (nal_type == H265NALUnitType::PPS)
	{
		AddPPS(nal_unit);
	}
}

uint8_t HEVCDecoderConfigurationRecord::Version()
{
	return _version;
}
uint8_t	HEVCDecoderConfigurationRecord::GeneralProfileSpace()
{
	return _general_profile_space;
}
uint8_t HEVCDecoderConfigurationRecord::GeneralTierFlag()
{
	return _general_tier_flag;
}
uint8_t HEVCDecoderConfigurationRecord::GeneralProfileIDC()
{
	return _general_profile_idc;
}
uint32_t HEVCDecoderConfigurationRecord::GeneralProfileCompatibilityFlags()
{
	return _general_profile_compatibility_flags;
}
uint64_t HEVCDecoderConfigurationRecord::GeneralConstraintIndicatorFlags()
{
	return _general_constraint_indicator_flags;
}
uint8_t HEVCDecoderConfigurationRecord::GeneralLevelIDC()
{
	return _general_level_idc;
}
uint16_t HEVCDecoderConfigurationRecord::MinSpatialSegmentationIDC()
{
	return _min_spatial_segmentation_idc;
}
uint8_t HEVCDecoderConfigurationRecord::ParallelismType()
{
	return _parallelism_type;
}
uint8_t HEVCDecoderConfigurationRecord::ChromaFormat()
{
	return _chroma_format;
}
uint8_t HEVCDecoderConfigurationRecord::BitDepthLumaMinus8()
{
	return _bit_depth_luma_minus8;
}
uint8_t HEVCDecoderConfigurationRecord::BitDepthChromaMinus8()
{
	return _bit_depth_chroma_minus8;
}
uint16_t HEVCDecoderConfigurationRecord::AvgFrameRate()
{
	return _avg_frame_rate;
}
uint8_t HEVCDecoderConfigurationRecord::ConstantFrameRate()
{
	return _constant_frame_rate;
}
uint8_t HEVCDecoderConfigurationRecord::NumTemporalLayers()
{
	return _num_temporal_layers;
}
uint8_t HEVCDecoderConfigurationRecord::TemporalIdNested()
{
	return _temporal_id_nested;
}
uint8_t HEVCDecoderConfigurationRecord::LengthSizeMinusOne()
{
	// lengthSizeMinusOne plus 1 indicates the length in bytes of the NALUnitLength field in an HEVC video sample in the stream to which this configuration record applies. For example, a size of one byte is indicated with a value of 0. The value of this field shall be one of 0, 1, or 3 corresponding to a length encoded with 1, 2, or 4 bytes, respectively.

	return _length_size_minus_one;
}
uint8_t HEVCDecoderConfigurationRecord::NumOfArrays()
{
	return _nal_units.size();
}
std::vector<std::shared_ptr<ov::Data>> HEVCDecoderConfigurationRecord::GetNalUnits(H265NALUnitType nal_type)
{
	if (_nal_units.find(static_cast<uint8_t>(nal_type)) == _nal_units.end())
	{
		return std::vector<std::shared_ptr<ov::Data>>();
	}

	return _nal_units[static_cast<uint8_t>(nal_type)];
}

bool HEVCDecoderConfigurationRecord::AddVPS(const std::shared_ptr<ov::Data> &nalu)
{
	H265VPS vps;

	if (H265Parser::ParseVPS(nalu->GetDataAs<uint8_t>(), nalu->GetLength(), vps) == false)
	{
		logte("Could not parse H265 VPS unit");
		return false;
	}

	if (_vps_map.find(vps.GetId()) != _vps_map.end())
	{
		return false;
	}

	_vps_map.emplace(vps.GetId(), vps);
	_vps_data_list.push_back(nalu);

	//_num_temporal_layers = std::max<uint8_t>(_num_temporal_layers, vps.GetMaxSubLayersMinus1() + 1);

	return true;
}

bool HEVCDecoderConfigurationRecord::AddSPS(const std::shared_ptr<ov::Data> &nalu)
{
	H265SPS sps;
	if (H265Parser::ParseSPS(nalu->GetDataAs<uint8_t>(), nalu->GetLength(), sps) == false)
	{
		logte("Could not parse H265 SPS unit");
		return false;
	}

	if (_sps_map.find(sps.GetId()) != _sps_map.end())
	{
		return false;
	}

	_h265_sps = sps;
	 
	auto profile_tier_level = sps.GetProfileTierLevel();
	_general_profile_space = profile_tier_level._general_profile_space;
	_general_tier_flag = profile_tier_level._general_tier_flag;
	_general_profile_idc = profile_tier_level._general_profile_idc;
	_general_profile_compatibility_flags = profile_tier_level._general_profile_compatibility_flags;
	_general_constraint_indicator_flags = profile_tier_level._general_constraint_indicator_flags;
	_general_level_idc = profile_tier_level._general_level_idc;

	_chroma_format = sps.GetChromaFormatIdc();
	_bit_depth_chroma_minus8 = sps.GetBitDepthChromaMinus8();
	_bit_depth_luma_minus8 = sps.GetBitDepthLumaMinus8();

	auto vui_parameters = sps.GetVuiParameters();
	_min_spatial_segmentation_idc = vui_parameters._min_spatial_segmentation_idc;

	// TODO(Getroot) : _num_temporal_layers must be the largest value among max_sub_layers_minus1 of VPS and max_sub_layers_minus1 of SPS.
	_num_temporal_layers = std::max<uint8_t>(_num_temporal_layers, sps.GetMaxSubLayersMinus1() + 1);
	_temporal_id_nested = sps.GetTemporalIdNestingFlag();
	_length_size_minus_one = 3;

	_avg_frame_rate = 0;
	_constant_frame_rate = 0;

	_sps_data_list.push_back(nalu);
	_sps_map.emplace(sps.GetId(), sps);

	return true;
}

bool HEVCDecoderConfigurationRecord::AddPPS(const std::shared_ptr<ov::Data> &nalu)
{
	H265PPS pps;

	if (H265Parser::ParsePPS(nalu->GetDataAs<uint8_t>(), nalu->GetLength(), pps) == false)
	{
		logte("Could not parse H265 PPS unit");
		return false;
	}

	if (_pps_map.find(pps.GetId()) != _pps_map.end())
	{
		return false;
	}

	_pps_map.emplace(pps.GetId(), pps);
	_pps_data_list.push_back(nalu);

	// TODO(Getroot) : Implement PPS parser for getting following values
	// _parallelism_type can be derived from the following PPS values:
	// if entropy coding sync enabled flag(1) && tiles enabled flag(1)
	// 		parallelism_type = 0 // mixed type parallel decoding
	// else if entropy coding sync enabled flag(1)
	// 		parallelism_type = 3 // wavefront-based parallel decoding
	// else if tiles enabled flag(1)
	// 		parallelism_type = 2 // tile-based parallel decoding
	// else 
	// 		parallelism_type = 1 // slice-based parallel decoding
	_parallelism_type = 0;

	return true;
}

std::tuple<std::shared_ptr<ov::Data>, FragmentationHeader> HEVCDecoderConfigurationRecord::GetVpsSpsPpsAsAnnexB()
{
	if (IsValid() == false)
	{
		return {nullptr, {}};
	}

	if (_vps_sps_pps_annexb_data != nullptr)
	{
		return {_vps_sps_pps_annexb_data, _vps_sps_pps_annexb_frag_header};
	}

	auto data = std::make_shared<ov::Data>(1024);
	FragmentationHeader frag_header;
	size_t offset = 0;

	for (auto &vps_data : _vps_data_list)
	{
		data->Append(H26X_START_CODE_PREFIX, H26X_START_CODE_PREFIX_LEN);
		offset += H26X_START_CODE_PREFIX_LEN;

		frag_header.AddFragment(offset, vps_data->GetLength());
		
		data->Append(vps_data);
		offset += vps_data->GetLength();
	}

	for (auto &sps_data : _sps_data_list)
	{
		data->Append(H26X_START_CODE_PREFIX, H26X_START_CODE_PREFIX_LEN);
		offset += H26X_START_CODE_PREFIX_LEN;

		frag_header.AddFragment(offset, sps_data->GetLength());
		
		data->Append(sps_data);
		offset += sps_data->GetLength();
	}

	for (auto &pps_data : _pps_data_list)
	{
		data->Append(H26X_START_CODE_PREFIX, H26X_START_CODE_PREFIX_LEN);
		offset += H26X_START_CODE_PREFIX_LEN;

		frag_header.AddFragment(offset, pps_data->GetLength());

		data->Append(pps_data);
		offset += pps_data->GetLength();
	}

	_vps_sps_pps_annexb_data = data;
	_vps_sps_pps_annexb_frag_header = frag_header;

	return {_vps_sps_pps_annexb_data, _vps_sps_pps_annexb_frag_header};
}

bool HEVCDecoderConfigurationRecord::AddVpsSpsPpsAnnexB(const std::shared_ptr<ov::Data> &data, FragmentationHeader *fragmentation_header)
{
	if ((data == nullptr) || (fragmentation_header == nullptr))
	{
		return false;
	}

	auto [vps_sps_pps_annexb, vps_sps_pps_frag_header] = GetVpsSpsPpsAsAnnexB();
	if (vps_sps_pps_annexb == nullptr)
	{
		return false;
	}

	data->Append(vps_sps_pps_annexb);
	fragmentation_header->AddFragments(&vps_sps_pps_frag_header);

	return true;
}

bool HEVCDecoderConfigurationRecord::AddAudAnnexB(const std::shared_ptr<ov::Data> &data, FragmentationHeader *fragmentation_header)
{
	if ((data == nullptr) || (fragmentation_header == nullptr))
	{
		return false;
	}

	data->Append(H26X_START_CODE_PREFIX, H26X_START_CODE_PREFIX_LEN);
	fragmentation_header->AddFragment(H26X_START_CODE_PREFIX_LEN, H265_AUD_SIZE);
	data->Append(H265_AUD, H265_AUD_SIZE);

	return true;
}
