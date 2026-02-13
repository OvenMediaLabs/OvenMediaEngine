#pragma once

#include <base/ovlibrary/ovlibrary.h>
#include <base/common_types.h>
#include <base/info/decoder_configuration_record.h>
#include "h265_types.h"
#include "h265_parser.h"

// ISO 14496-15, 8.3.3.1
// aligned(8) class HEVCDecoderConfigurationRecord {
// 	unsigned int(8) configurationVersion = 1;
// 	unsigned int(2) general_profile_space;
// 	unsigned int(1) general_tier_flag;
// 	unsigned int(5) general_profile_idc;
// 	unsigned int(32) general_profile_compatibility_flags;
// 	unsigned int(48) general_constraint_indicator_flags;
// 	unsigned int(8) general_level_idc;
// 	bit(4) reserved = ‘1111’b;
// 	unsigned int(12) min_spatial_segmentation_idc;
// 	bit(6) reserved = ‘111111’b;
// 	unsigned int(2) parallelismType;
// 	bit(6) reserved = ‘111111’b;
// 	unsigned int(2) chromaFormat;
// 	bit(5) reserved = ‘11111’b;
// 	unsigned int(3) bitDepthLumaMinus8;
// 	bit(5) reserved = ‘11111’b;
// 	unsigned int(3) bitDepthChromaMinus8;
// 	bit(16) avgFrameRate;
// 	bit(2) constantFrameRate;
// 	bit(3) numTemporalLayers;
// 	bit(1) temporalIdNested;
// 	unsigned int(2) lengthSizeMinusOne; 
// 	unsigned int(8) numOfArrays;
// 	for (j=0; j < numOfArrays; j++) {
// 		bit(1) array_completeness;
// 		unsigned int(1) reserved = 0;
// 		unsigned int(6) NAL_unit_type;
// 		unsigned int(16) numNalus;
// 		for (i=0; i< numNalus; i++) {
// 			unsigned int(16) nalUnitLength;
// 			bit(8*nalUnitLength) nalUnit;
// 		}
// 	}
// }

#define MIN_HEVCDECODERCONFIGURATIONRECORD_SIZE	23

class HEVCDecoderConfigurationRecord : public DecoderConfigurationRecord
{
public:
	bool IsValid() const override;
	ov::String GetCodecsParameter() const override;
	bool Parse(const std::shared_ptr<const ov::Data> &data) override;
	bool Equals(const std::shared_ptr<DecoderConfigurationRecord> &other) override;

	MAY_THROWS(ov::BitReaderError)
	static std::shared_ptr<HEVCDecoderConfigurationRecord> ParseV2(ov::BitReader &reader)
	{
		auto record = std::make_shared<HEVCDecoderConfigurationRecord>();
		if (record->ParseV2Internal(reader) == false)
		{
			return nullptr;
		}
		return record;
	}

	std::shared_ptr<const ov::Data> Serialize() override;

	void AddNalUnit(H265NALUnitType nal_type, const std::shared_ptr<ov::Data> &nal_unit); // SPS, PPS, VPS, etc.

	uint8_t Version();
	uint8_t	GeneralProfileSpace();
	uint8_t GeneralTierFlag();
	uint8_t GeneralProfileIDC();
	uint32_t GeneralProfileCompatibilityFlags();
	uint64_t GeneralConstraintIndicatorFlags();
	uint8_t GeneralLevelIDC();
	uint16_t MinSpatialSegmentationIDC();
	uint8_t ParallelismType();
	uint8_t ChromaFormat();
	uint8_t BitDepthLumaMinus8();
	uint8_t BitDepthChromaMinus8();
	uint16_t AvgFrameRate();
	uint8_t ConstantFrameRate();
	uint8_t NumTemporalLayers();
	uint8_t TemporalIdNested();
	uint8_t LengthSizeMinusOne();
	uint8_t NumOfArrays();
	
	// Get NAL units by NAL unit type
	std::vector<std::shared_ptr<ov::Data>> GetNalUnits(H265NALUnitType nal_type);

	// Helpers
	int32_t GetWidth();
	int32_t GetHeight();

	std::tuple<std::shared_ptr<ov::Data>, FragmentationHeader> GetVpsSpsPpsAsAnnexB();
	bool AddVpsSpsPpsAnnexB(const std::shared_ptr<ov::Data> &data, FragmentationHeader *fragmentation_header);
	bool AddAudAnnexB(const std::shared_ptr<ov::Data> &data, FragmentationHeader *fragmentation_header);

private:
	MAY_THROWS(ov::BitReaderError)
	bool ParseV2Internal(ov::BitReader &reader);

	bool AddVPS(const std::shared_ptr<ov::Data> &nalu);
	bool AddSPS(const std::shared_ptr<ov::Data> &nalu);
	bool AddPPS(const std::shared_ptr<ov::Data> &nalu);

private:
	// Used to serialize
	uint8_t _version = 1;
	uint8_t _general_profile_space = 0;
	uint8_t _general_tier_flag = 0;
	uint8_t _general_profile_idc = 0;
	uint32_t _general_profile_compatibility_flags = 0;
	uint64_t _general_constraint_indicator_flags = 0;
	uint8_t _general_level_idc = 0;
	uint16_t _min_spatial_segmentation_idc = 0; 
	uint8_t _parallelism_type = 0; // from PPS
	uint8_t _chroma_format = 0;
	uint8_t _bit_depth_luma_minus8 = 0;
	uint8_t _bit_depth_chroma_minus8 = 0;
	uint16_t _avg_frame_rate = 0; // Value 0 indicates an unspecified average frame rate.
	uint8_t _constant_frame_rate = 0; // Value 0 indicates that the stream may or may not be of constant frame rate.
	uint8_t _num_temporal_layers = 0; 
	uint8_t _temporal_id_nested = 0;
	uint8_t _length_size_minus_one = 3; // 4 bytes by default

	// NAL unit type -> NAL unit vector
	std::map<uint8_t, std::vector<std::shared_ptr<ov::Data>>> _nal_units;

	// Extra data
	std::vector<std::shared_ptr<ov::Data>>	_vps_data_list;
	// vps_id, vps
	std::map<uint8_t, H265VPS> _vps_map;

	std::vector<std::shared_ptr<ov::Data>>	_sps_data_list;
	// sps_id, sps
	std::map<uint8_t, H265SPS> _sps_map;

	std::vector<std::shared_ptr<ov::Data>>	_pps_data_list;
	// pps_id, pps
	std::map<uint8_t, H265PPS> _pps_map;

	H265SPS _h265_sps;
	std::shared_ptr<ov::Data> _vps_sps_pps_annexb_data = nullptr;
	FragmentationHeader _vps_sps_pps_annexb_frag_header;
};
