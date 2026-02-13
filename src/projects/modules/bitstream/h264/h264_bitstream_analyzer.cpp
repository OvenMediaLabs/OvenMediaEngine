#include <modules/bitstream/nalu/nal_unit_bitstream_parser.h>

#include "h264_bitstream_analyzer.h"
#include "h264_common.h"

#include <base/ovlibrary/log.h>

#define OV_LOG_TAG "H264BitstreamAnalyzer"

void H264BitstreamAnalyzer::ValidateNalUnit(const uint8_t *nal_unit_payload, size_t length, uint8_t nal_unit_header)
{
	const uint8_t nal_unit_type = nal_unit_header & kH264NalUnitTypeMask;
	if (IsValidH264NalUnitType(nal_unit_type) == false)
	{
		logte("Invalid NAL unit type %u", nal_unit_type);
		return;
	}
	ValidateNalUnit(nal_unit_payload, length, static_cast<H264NalUnitType>(nal_unit_type), nal_unit_header);
}

void H264BitstreamAnalyzer::ValidateNalUnit(const uint8_t *nal_unit_payload, size_t length, H264NalUnitType nal_unit_type, uint8_t nal_unit_header)
{
	if (nal_unit_type == H264NalUnitType::Sps || nal_unit_type == H264NalUnitType::Pps)
	{
		if (GET_NAL_REF_IDC(nal_unit_header) != kH264SpsPpsNalRefIdc)
		{
			logte("Invalid nal_ref_idc value for unit type %u", nal_unit_type);
			return;
		}
		NalUnitBitstreamParser parser(nal_unit_payload, length);
		if (nal_unit_type == H264NalUnitType::Sps)
		{
			// Skip profile_idc, constraint bits, reserved bits and level_idc
			parser.Skip(24);
		}
		uint32_t id = 0;
		if (parser.ReadUEV(id))
		{
			if (nal_unit_type == H264NalUnitType::Sps)
			{
				if (known_sps_ids_.find(id) == known_sps_ids_.end())
				{
					known_sps_ids_.emplace(id);                                        
				}
			}
			else
			{
				if (known_pps_ids_.find(id) == known_pps_ids_.end())
				{
					known_pps_ids_.emplace(id);
				}
				uint32_t sps_id;
				if (parser.ReadUEV(sps_id) && known_sps_ids_.find(id) == known_sps_ids_.end())
				{
					logte("PPS with id %u references an unknown SPS with id %u", id, sps_id);
				}
			}
		}
		else
		{
			logte("Failed to read id of %s", nal_unit_type == H264NalUnitType::Sps ? "SPS" : "PPS");
		}
		
	}
	else if (nal_unit_type == H264NalUnitType::IdrSlice || nal_unit_type == H264NalUnitType::NonIdrSlice)
	{
		/*
			7.3.2.8 Slice layer without partitioning RBSP syntax

			slice_layer_without_partitioning_rbsp( ) {
				slice_header()
				slice_data()
				rbsp_slice_trailing_bits()
			}
		*/
		NalUnitBitstreamParser parser(nal_unit_payload, length);
		uint32_t first_mb_in_slice, slice_type, pps_id;
		if (parser.ReadUEV(first_mb_in_slice) && parser.ReadUEV(slice_type) && parser.ReadUEV(pps_id))
		{
			const auto nal_ref_idc = GET_NAL_REF_IDC(nal_unit_header);
			int16_t expected_nal_ref_idc = GetSliceTypeNalRefIdc(static_cast<uint8_t>(slice_type));
			if (expected_nal_ref_idc != -1 && nal_ref_idc != expected_nal_ref_idc)
			{
				logte("Invalid nal_ref_idc value %u for NAL unit type %u/slice type %u, expected value is %u", nal_ref_idc, nal_unit_type, slice_type, expected_nal_ref_idc);
				return;
			}
			if (known_pps_ids_.find(pps_id) == known_pps_ids_.end())
			{
				logte("Slice with type %u references a PPS with unknown id %u", nal_unit_type, pps_id);
			}
		}
		else
		{
			logte("Failed to parse slice with type %u", nal_unit_type);
		}
	}
}