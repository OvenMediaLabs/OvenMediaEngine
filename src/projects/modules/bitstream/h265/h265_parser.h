// I developed the code of this file by referring to the source code of virinext's hevcsbrowser (https://github.com/virinext/hevcesbrowser). 
// Thanks to virinext.
// - Getroot

#pragma once

#include <base/ovlibrary/ovlibrary.h>
#include <modules/bitstream/nalu/nal_unit_bitstream_parser.h>
#include <stdint.h>

#include "h265_types.h"


constexpr size_t H265_NAL_UNIT_HEADER_SIZE = 2;
struct ProfileTierLevel
{
	uint8_t _general_profile_space = 0;
	uint8_t _general_tier_flag = 0;
    uint8_t _general_profile_idc = 0;
	uint32_t _general_profile_compatibility_flags = 0;
	uint64_t _general_constraint_indicator_flags = 0;
    uint8_t _general_level_idc = 0;

	ov::String ToString() const
	{
		return ov::String::FormatString("profile_space: %u, tier_flag: %u, profile_idc: %u, profile_compatibility_flags: %u, constraint_indicator_flags: %" PRIu64 ", level_idc: %u",
			_general_profile_space, _general_tier_flag, _general_profile_idc, _general_profile_compatibility_flags, _general_constraint_indicator_flags, _general_level_idc);
	}
};

struct HrdParameters
{

};

struct SubLayerHrdParameters
{

};

struct VuiParameters
{
public:
    struct ASPECT_RATIO
    {
        uint16_t _width;
        uint16_t _height;
    };

    ASPECT_RATIO _aspect_ratio = { 0, 0 };
    uint8_t _aspect_ratio_idc = 0;
    uint32_t _num_units_in_tick = 0;
    uint32_t _time_scale = 0;
	uint32_t _min_spatial_segmentation_idc = 0;
};

struct ShortTermRefPicSet
{
public:
    uint8_t                   inter_ref_pic_set_prediction_flag;
    uint32_t                  delta_idx_minus1;
    uint8_t                   delta_rps_sign;
    uint32_t                  abs_delta_rps_minus1;
    std::vector<uint8_t>      used_by_curr_pic_flag;
    std::vector<uint8_t>      use_delta_flag;
    uint32_t                  num_negative_pics;
    uint32_t                  num_positive_pics;
    std::vector<uint32_t>     delta_poc_s0_minus1;
    std::vector<uint8_t>      used_by_curr_pic_s0_flag;
    std::vector<uint32_t>     delta_poc_s1_minus1;
    std::vector<uint8_t>      used_by_curr_pic_s1_flag;
};

class H265NalUnitHeader
{
public:
    H265NALUnitType GetNalUnitType()
    {
        return _type;
    }

    uint8_t GetLayerId()
    {
        return _layer_id;
    }

    uint8_t GetTemporalIdPlus1()
    {
        return _temporal_id_plus1;
    }
private:
    H265NALUnitType _type;
    uint8_t _layer_id = 0;
    uint8_t _temporal_id_plus1 = 0;

    friend class H265Parser;
};

struct H265VPS
{
	uint8_t GetId() const
	{
		return vps_video_parameter_set_id;
	}

	uint8_t vps_video_parameter_set_id;
};

class H265SPS
{
public:
    uint32_t GetWidth() const
    {
        return _width;
    }

    uint32_t GetHeight() const
    {
        return _height;
    }

    const ProfileTierLevel& GetProfileTierLevel() const
    {
        return _profile_tier_level;
    }

	const VuiParameters& GetVuiParameters() const
	{
		return _vui_parameters;
	}

    float GetFps() const
    {
        return _vui_parameters._time_scale / _vui_parameters._num_units_in_tick;
    }

    uint32_t GetId() const
    {
        return _id;
    }

	uint32_t GetChromaFormatIdc() const
	{
		return _chroma_format_idc;
	}

	uint32_t GetBitDepthLumaMinus8() const
	{
		return _bit_depth_luma_minus8;
	}

	uint32_t GetBitDepthChromaMinus8() const
	{
		return _bit_depth_chroma_minus8;
	}

	uint8_t GetMaxSubLayersMinus1() const
	{
		return _max_sub_layers_minus1;
	}

	bool GetTemporalIdNestingFlag() const
	{
		return _temporal_id_nesting_flag;
	}

    ov::String GetInfoString()
    {
        ov::String out_str = ov::String::FormatString("\n[H265Sps]\n");

        out_str.AppendFormat("\tProfileTierLevel\n");
		out_str.AppendFormat("\t%s\n", _profile_tier_level.ToString().CStr());

        out_str.AppendFormat("\tWidth(%d)\n", GetWidth());
        out_str.AppendFormat("\tHeight(%d)\n", GetHeight());
        out_str.AppendFormat("\tFps(%.2f)\n", GetFps());
        out_str.AppendFormat("\tId(%d)\n", GetId());
        out_str.AppendFormat("\tAspectRatio(IDC: %d, Extented : %d:%d)\n", _vui_parameters._aspect_ratio_idc, _vui_parameters._aspect_ratio._width, _vui_parameters._aspect_ratio._height);

        return out_str;
    }

private:
    unsigned int _width = 0;
    unsigned int _height = 0;
    unsigned int _fps = 0;
    unsigned int _id = 0;

    ProfileTierLevel _profile_tier_level;
    VuiParameters   _vui_parameters;

	uint8_t _max_sub_layers_minus1 = 0;
	bool _temporal_id_nesting_flag = false;

	uint32_t _chroma_format_idc = 0;
	uint32_t _bit_depth_luma_minus8 = 0;
	uint32_t _bit_depth_chroma_minus8 = 0;

    friend class H265Parser;
};

struct H265PPS
{
public:
	uint32_t GetId() const
	{
		return pps_pic_parameter_set_id;
	}

	uint32_t GetSpsId() const
	{
		return pps_seq_parameter_set_id;
	}

	uint32_t pps_pic_parameter_set_id;
	uint32_t pps_seq_parameter_set_id;
};

class H265Parser
{
public:
	// returns offset (start point), code_size : 3(001) or 4(0001)
	// returns -1 if there is no start code in the buffer
	static int FindAnnexBStartCode(const uint8_t *bitstream, size_t length, size_t &start_code_size);
    static bool CheckKeyframe(const uint8_t *bitstream, size_t length);
    static bool ParseNalUnitHeader(const uint8_t *nalu, size_t length, H265NalUnitHeader &header);
	static bool ParseNalUnitHeader(const std::shared_ptr<const ov::Data> &nalu, H265NalUnitHeader &header);
	static bool ParseVPS(const uint8_t *nalu, size_t length, H265VPS &vps);
    static bool ParseSPS(const uint8_t *nalu, size_t length, H265SPS &sps);
	static bool ParsePPS(const uint8_t *nalu, size_t length, H265PPS &pps);

private:
    static bool ParseNalUnitHeader(NalUnitBitstreamParser &parser, H265NalUnitHeader &header);
    static bool ProcessProfileTierLevel(uint32_t max_sub_layers_minus1, NalUnitBitstreamParser &parser, ProfileTierLevel &profile);
    static bool ProcessShortTermRefPicSet(uint32_t idx, uint32_t num_short_term_ref_pic_sets, const std::vector<ShortTermRefPicSet> &rpset_list, NalUnitBitstreamParser &parser, ShortTermRefPicSet &rpset);
    static bool ProcessVuiParameters(uint32_t max_sub_layers_minus1, NalUnitBitstreamParser &parser, VuiParameters &params);
    static bool ProcessHrdParameters(uint8_t common_inf_present_flag, uint32_t max_sub_layers_minus1, NalUnitBitstreamParser &parser, HrdParameters &params);
    static bool ProcessSubLayerHrdParameters(uint8_t sub_pic_hrd_params_present_flag, uint32_t cpb_cnt, NalUnitBitstreamParser &parser, SubLayerHrdParameters &params);
};
