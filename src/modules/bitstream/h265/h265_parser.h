// I developed the code of this file by referring to the source code of virinext's hevcsbrowser (https://github.com/virinext/hevcesbrowser).
// Thanks to virinext.
// - Getroot
//
// Bitstream syntax and semantics follow Rec. ITU-T H.265 (HEVC) | ISO/IEC 23008-2.
// Standard document (all versions): https://www.itu.int/rec/T-REC-H.265
// The section numbers cited throughout this file (e.g. 7.3.2.2 SPS, 7.3.2.3 PPS,
// 7.3.6.1 slice_segment_header) refer to that specification.

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

	// VCL NAL units (coded slice segments) have NAL unit type in the range [0, 31].
	// Rec. ITU-T H.265 Table 7-1.
	bool IsVideoSlice() const
	{
		return ov::ToUnderlyingType(_type) <= 31;
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

// Sequence Parameter Set (Rec. ITU-T H.265, 7.3.2.2)
// Sequence-level parameters shared by every picture that references this SPS.
// Variable-length bitstream: u(n)=n bits, ue(v)=Exp-Golomb unsigned, se(v)=signed. '*' = stored by OME.
//
//   syntax element                                | desc  | present when
//   ----------------------------------------------|-------|-------------------------------------
//   sps_video_parameter_set_id                    | u(4)  |
//   sps_max_sub_layers_minus1                     | u(3)  | *
//   sps_temporal_id_nesting_flag                  | u(1)  | *
//   profile_tier_level()                          |  ...  | *  (profile/tier/level, 7.3.3)
//   sps_seq_parameter_set_id                      | ue(v) |
//   chroma_format_idc                             | ue(v) | *
//   separate_colour_plane_flag                    | u(1)  | *  chroma_format_idc == 3
//   pic_width_in_luma_samples                     | ue(v) | *
//   pic_height_in_luma_samples                    | ue(v) | *
//   conformance_window_flag                       | u(1)  |
//     conf_win_{left,right,top,bottom}_offset     | ue(v) |    if conformance_window_flag
//   bit_depth_luma_minus8                         | ue(v) | *
//   bit_depth_chroma_minus8                       | ue(v) | *
//   log2_max_pic_order_cnt_lsb_minus4             | ue(v) | *
//   sps_sub_layer_ordering_info_present_flag      | u(1)  |
//     max_dec_pic_buffering/reorder/latency[i]    | ue(v) |    per sub-layer
//   log2_min_luma_coding_block_size_minus3        | ue(v) | *  -.
//   log2_diff_max_min_luma_coding_block_size      | ue(v) | *  -'-> CtbSizeY / PicSizeInCtbsY
//   log2_min/diff_max_min transform block size    | ue(v) |
//   max_transform_hierarchy_depth_inter/intra     | ue(v) |
//   scaling_list_enabled_flag [+ scaling_list()]  | u(1)  |
//   amp_enabled_flag                              | u(1)  |
//   sample_adaptive_offset_enabled_flag           | u(1)  | *
//   pcm_enabled_flag [+ pcm params]               | u(1)  |
//   num_short_term_ref_pic_sets                   | ue(v) | *
//     st_ref_pic_set(i)                           |  ...  | *  loop, 7.3.7
//   long_term_ref_pics_present_flag               | u(1)  | *
//     num_long_term_ref_pics_sps [+ lt entries]   | ue(v) | *  if long_term_ref_pics_present
//   sps_temporal_mvp_enabled_flag                 | u(1)  | *
//   strong_intra_smoothing_enabled_flag           | u(1)  |
//   vui_parameters_present_flag [+ vui()]         | u(1)  | *  VUI -> frame rate, SAR
//   sps_extension_flag                            | u(1)  |
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

	// ChromaArrayType (Rec. ITU-T H.265 eq. 7-10)
	uint32_t GetChromaArrayType() const
	{
		return (_separate_colour_plane_flag == 1) ? 0 : _chroma_format_idc;
	}

	// PicSizeInCtbsY (Rec. ITU-T H.265 eq. 7-15..7-20) - used to size slice_segment_address.
	uint32_t GetPicSizeInCtbsY() const
	{
		const uint32_t min_cb_log2_size_y = _log2_min_luma_coding_block_size_minus3 + 3;
		const uint32_t ctb_log2_size_y = min_cb_log2_size_y + _log2_diff_max_min_luma_coding_block_size;

		// CtbLog2SizeY is 4..6 for conformant streams (Rec. ITU-T H.265 7.4.3.2.1; CtbSizeY in {16,32,64}).
		if (ctb_log2_size_y < 4 || ctb_log2_size_y > 6)
		{
			return 0;
		}

		const uint32_t ctb_size_y = 1u << ctb_log2_size_y;
		const uint32_t pic_width_in_ctbs_y = (_pic_width_in_luma_samples + ctb_size_y - 1) / ctb_size_y;
		const uint32_t pic_height_in_ctbs_y = (_pic_height_in_luma_samples + ctb_size_y - 1) / ctb_size_y;
		return pic_width_in_ctbs_y * pic_height_in_ctbs_y;
	}

private:
    unsigned int _width = 0;
    unsigned int _height = 0;
    [[maybe_unused]] unsigned int _fps = 0;
    unsigned int _id = 0;

    ProfileTierLevel _profile_tier_level;
    VuiParameters   _vui_parameters;

	uint8_t _max_sub_layers_minus1 = 0;
	bool _temporal_id_nesting_flag = false;

	uint32_t _chroma_format_idc = 0;
	uint32_t _bit_depth_luma_minus8 = 0;
	uint32_t _bit_depth_chroma_minus8 = 0;

	// Fields required to parse the slice_segment_header (Rec. ITU-T H.265 7.3.6.1)
	uint8_t _separate_colour_plane_flag = 0;
	uint32_t _pic_width_in_luma_samples = 0;
	uint32_t _pic_height_in_luma_samples = 0;
	uint32_t _log2_min_luma_coding_block_size_minus3 = 0;
	uint32_t _log2_diff_max_min_luma_coding_block_size = 0;
	uint32_t _log2_max_pic_order_cnt_lsb_minus4 = 0;
	uint8_t _sample_adaptive_offset_enabled_flag = 0;
	uint8_t _sps_temporal_mvp_enabled_flag = 0;
	uint8_t _long_term_ref_pics_present_flag = 0;
	uint32_t _num_long_term_ref_pics_sps = 0;
	std::vector<uint8_t> _used_by_curr_pic_lt_sps_flag;
	uint32_t _num_short_term_ref_pic_sets = 0;
	std::vector<ShortTermRefPicSet> _short_term_ref_pic_sets;

    friend class H265Parser;
};

// Picture Parameter Set (Rec. ITU-T H.265, 7.3.2.3)
// Picture-level parameters. u(n)=n bits, ue(v)=Exp-Golomb unsigned, se(v)=signed. '*' = stored by OME
// (the stored flags are exactly those needed to know which slice_segment_header elements are present).
//
//   syntax element                                | desc  | present when
//   ----------------------------------------------|-------|-------------------------------------
//   pps_pic_parameter_set_id                      | ue(v) | *
//   pps_seq_parameter_set_id                      | ue(v) | *  -> SPS
//   dependent_slice_segments_enabled_flag         | u(1)  | *
//   output_flag_present_flag                      | u(1)  | *
//   num_extra_slice_header_bits                   | u(3)  | *
//   sign_data_hiding_enabled_flag                 | u(1)  |
//   cabac_init_present_flag                       | u(1)  | *
//   num_ref_idx_l0_default_active_minus1          | ue(v) | *
//   num_ref_idx_l1_default_active_minus1          | ue(v) | *
//   init_qp_minus26                               | se(v) |
//   constrained_intra_pred_flag                   | u(1)  |
//   transform_skip_enabled_flag                   | u(1)  |
//   cu_qp_delta_enabled_flag [+ diff_cu_qp_depth] | u(1)  |
//   pps_cb_qp_offset / pps_cr_qp_offset           | se(v) |
//   pps_slice_chroma_qp_offsets_present_flag      | u(1)  | *
//   weighted_pred_flag                            | u(1)  | *
//   weighted_bipred_flag                          | u(1)  | *
//   transquant_bypass_enabled_flag                | u(1)  |
//   tiles_enabled_flag                            | u(1)  | *
//   entropy_coding_sync_enabled_flag              | u(1)  | *
//     tile columns/rows params                    | ue(v) |    if tiles_enabled_flag
//   pps_loop_filter_across_slices_enabled_flag    | u(1)  | *
//   deblocking_filter_control_present_flag        | u(1)  |
//     deblocking_filter_override_enabled_flag     | u(1)  | *
//     pps_deblocking_filter_disabled_flag         | u(1)  | *
//     pps_beta_offset_div2 / pps_tc_offset_div2   | se(v) |    if !pps_deblocking_filter_disabled
//   pps_scaling_list_data_present_flag [+ data]   | u(1)  |
//   lists_modification_present_flag               | u(1)  | *
//   log2_parallel_merge_level_minus2              | ue(v) |
//   slice_segment_header_extension_present_flag   | u(1)  | *
//   pps_extension_present_flag                    | u(1)  |
//     pps_range_extension_flag                    | u(1)  | *  fail-safed (not parsed)
//     pps_scc_extension_flag                      | u(1)  | *  fail-safed (not parsed)
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

	uint32_t pps_pic_parameter_set_id = 0;
	uint32_t pps_seq_parameter_set_id = 0;

	// Fields required to parse the slice_segment_header (Rec. ITU-T H.265 7.3.6.1)
	uint8_t dependent_slice_segments_enabled_flag = 0;
	uint8_t output_flag_present_flag = 0;
	uint8_t num_extra_slice_header_bits = 0;
	uint8_t cabac_init_present_flag = 0;
	uint32_t num_ref_idx_l0_default_active_minus1 = 0;
	uint32_t num_ref_idx_l1_default_active_minus1 = 0;
	uint8_t weighted_pred_flag = 0;
	uint8_t weighted_bipred_flag = 0;
	uint8_t pps_slice_chroma_qp_offsets_present_flag = 0;
	uint8_t deblocking_filter_override_enabled_flag = 0;
	uint8_t pps_deblocking_filter_disabled_flag = 0;
	uint8_t pps_loop_filter_across_slices_enabled_flag = 0;
	uint8_t lists_modification_present_flag = 0;
	uint8_t tiles_enabled_flag = 0;
	uint8_t entropy_coding_sync_enabled_flag = 0;
	uint8_t slice_segment_header_extension_present_flag = 0;

	// Extension flags. When set, the slice_segment_header may contain additional
	// conditional syntax that this parser does not handle; treated as fail-safe.
	uint8_t pps_range_extension_flag = 0;
	uint8_t pps_scc_extension_flag = 0;
};

// Slice segment header (Rec. ITU-T H.265, 7.3.6.1)
// H265Parser::ParseSliceHeader walks the header (in the order below) through byte_alignment() to
// measure its size; only slice_type and the header size are retained ('*'). u(v) lengths shown inline.
//
//   syntax element                                | desc  | present when
//   ----------------------------------------------|-------|-------------------------------------
//   first_slice_segment_in_pic_flag               | u(1)  |
//   no_output_of_prior_pics_flag                  | u(1)  | IRAP NAL (type 16..23)
//   slice_pic_parameter_set_id                    | ue(v) |    -> PPS/SPS
//   dependent_slice_segment_flag                  | u(1)  | !first && dependent_slices_enabled
//   slice_segment_address                         | u(v)  | !first; v=Ceil(Log2(PicSizeInCtbsY))
//   --- following present only if !dependent_slice_segment_flag ---
//   slice_reserved_flag[i]                        | u(1)  | i < num_extra_slice_header_bits
//   slice_type                                    | ue(v) | *  0=B, 1=P, 2=I
//   pic_output_flag                               | u(1)  | output_flag_present
//   colour_plane_id                               | u(2)  | separate_colour_plane
// ....(omitted)....
class H265SliceHeader
{
public:
	enum class SliceType : uint8_t
	{
		BSlice = 0,
		PSlice = 1,
		ISlice = 2
	};

	SliceType GetSliceType() const
	{
		return static_cast<SliceType>(_slice_type);
	}

	size_t GetHeaderSizeInBits() const
	{
		return _header_size_in_bits;
	}

	size_t GetHeaderSizeInBytes() const
	{
		return ((_header_size_in_bits + 7) / 8);
	}

private:
	uint32_t _slice_type = 2;
	size_t _header_size_in_bits = 0;

	friend class H265Parser;
};

class HEVCDecoderConfigurationRecord;

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

	// Parses the slice_segment_header up to (and including) byte_alignment() so that the
	// header size in bytes can be used to split CENC clear/protected byte ranges.
	static bool ParseSliceHeader(const uint8_t *nalu, size_t length, H265SliceHeader &header, const std::shared_ptr<HEVCDecoderConfigurationRecord> &hvcc);

private:
    static bool ParseNalUnitHeader(NalUnitBitstreamParser &parser, H265NalUnitHeader &header);
    static bool ProcessProfileTierLevel(uint32_t max_sub_layers_minus1, NalUnitBitstreamParser &parser, ProfileTierLevel &profile);
    static bool ProcessShortTermRefPicSet(uint32_t idx, uint32_t num_short_term_ref_pic_sets, const std::vector<ShortTermRefPicSet> &rpset_list, NalUnitBitstreamParser &parser, ShortTermRefPicSet &rpset);
    static bool ProcessVuiParameters(uint32_t max_sub_layers_minus1, NalUnitBitstreamParser &parser, VuiParameters &params);
    static bool ProcessHrdParameters(uint8_t common_inf_present_flag, uint32_t max_sub_layers_minus1, NalUnitBitstreamParser &parser, HrdParameters &params);
    static bool ProcessSubLayerHrdParameters(uint8_t sub_pic_hrd_params_present_flag, uint32_t cpb_cnt, NalUnitBitstreamParser &parser, SubLayerHrdParameters &params);
    static bool ProcessPredWeightTable(NalUnitBitstreamParser &parser, uint32_t chroma_array_type, uint32_t num_ref_idx_l0_active_minus1, uint32_t num_ref_idx_l1_active_minus1, bool is_b_slice);
};
