// I developed the code of this file by referring to the source code of virinext's hevcsbrowser (https://github.com/virinext/hevcesbrowser).
// Thanks to virinext.
// - Getroot

#include "h265_parser.h"

#include "h265_types.h"

#define OV_LOG_TAG "H265Parser"

// returns offset (start point), code_size : 3(001) or 4(0001)
// returns -1 if there is no start code in the buffer
int H265Parser::FindAnnexBStartCode(const uint8_t *bitstream, size_t length, size_t &start_code_size)
{
	size_t offset = 0;
	start_code_size = 0;

	while (offset < length)
	{
		size_t remaining = length - offset;
		const uint8_t *data = bitstream + offset;

		if ((remaining >= 3 && data[0] == 0x00 && data[1] == 0x00 && data[2] == 0x01) ||
			(remaining >= 4 && data[0] == 0x00 && data[1] == 0x00 && data[2] == 0x00 && data[3] == 0x01))
		{
			if (data[2] == 0x01)
			{
				start_code_size = 3;
			}
			else
			{
				start_code_size = 4;
			}

			return offset;
		}
		else
		{
			offset += 1;
		}
	}

	return -1;
}

bool H265Parser::CheckKeyframe(const uint8_t *bitstream, size_t length)
{
	size_t offset = 0;
	while (offset < length)
	{
		size_t remaining = length - offset;
		const uint8_t *data = bitstream + offset;

		if ((remaining >= 3 && data[0] == 0x00 && data[1] == 0x00 && data[2] == 0x01) ||
			(remaining >= 4 && data[0] == 0x00 && data[1] == 0x00 && data[2] == 0x00 && data[3] == 0x01))
		{
			if (data[2] == 0x01)
			{
				offset += 3;
			}
			else
			{
				offset += 4;
			}

			if (length - offset > H265_NAL_UNIT_HEADER_SIZE)
			{
				H265NalUnitHeader header;
				ParseNalUnitHeader(bitstream + offset, H265_NAL_UNIT_HEADER_SIZE, header);

				if (header.GetNalUnitType() == H265NALUnitType::IDR_W_RADL ||
					header.GetNalUnitType() == H265NALUnitType::CRA_NUT ||
					header.GetNalUnitType() == H265NALUnitType::BLA_W_RADL)
				{
					return true;
				}
			}
		}
		else
		{
			offset++;
		}
	}
	return false;
}

bool H265Parser::ParseNalUnitHeader(const uint8_t *nalu, size_t length, H265NalUnitHeader &header)
{
	if (length < H265_NAL_UNIT_HEADER_SIZE)
	{
		logte("Invalid NALU header size: %zu", length);
		return false;
	}

	uint8_t forbidden_zero_bit = (nalu[0] >> 7) & 0x01;
	if (forbidden_zero_bit != 0)
	{
		logte("Invalid NALU header: forbidden_zero_bit is not 0");
		return false;
	}

	uint8_t nal_type = (nalu[0] >> 1) & 0x3F;
	header._type = static_cast<H265NALUnitType>(nal_type);

	uint8_t layer_id = ((nalu[0] & 0x01) << 5) | ((nalu[1] >> 3) & 0x1F);
	header._layer_id = layer_id;

	uint8_t temporal_id_plus1 = nalu[1] & 0x07;
	header._temporal_id_plus1 = temporal_id_plus1;

	return true;
}

bool H265Parser::ParseNalUnitHeader(const std::shared_ptr<const ov::Data> &nalu, H265NalUnitHeader &header)
{
	return (nalu != nullptr) ? ParseNalUnitHeader(nalu->GetDataAs<uint8_t>(), nalu->GetLength(), header) : false;
}

bool H265Parser::ParseNalUnitHeader(NalUnitBitstreamParser &parser, H265NalUnitHeader &header)
{
	// forbidden_zero_bit
	uint8_t forbidden_zero_bit;
	if (parser.ReadBits(1, forbidden_zero_bit) == false)
	{
		return false;
	}

	if (forbidden_zero_bit != 0)
	{
		return false;
	}

	// type
	uint8_t nal_type;
	if (parser.ReadBits(6, nal_type) == false)
	{
		return false;
	}

	header._type = static_cast<H265NALUnitType>(nal_type);

	uint8_t layer_id;
	if (parser.ReadBits(6, layer_id) == false)
	{
		return false;
	}

	header._layer_id = layer_id;

	uint8_t temporal_id_plus1;
	if (parser.ReadBits(3, temporal_id_plus1) == false)
	{
		return false;
	}

	header._temporal_id_plus1 = temporal_id_plus1;
	return true;
}

#define H265_READ_BITS(value, bits)            \
	value;                                     \
	if (parser.ReadBits(bits, value) == false) \
	{                                          \
		return false;                          \
	}

#define H265_READ_UEV(value)            \
	value;                              \
	if (parser.ReadUEV(value) == false) \
	{                                   \
		return false;                   \
	}

#define H265_READ_SEV(value)            \
	value;                              \
	if (parser.ReadSEV(value) == false) \
	{                                   \
		return false;                   \
	}

// Rec. ITU-T H.265 (V10), 7.3.2.1 Video parameter set RBSP syntax
bool H265Parser::ParseVPS(const uint8_t *nalu, size_t length, H265VPS &vps)
{
	NalUnitBitstreamParser parser(nalu, length);

	H265NalUnitHeader header;

	if (ParseNalUnitHeader(parser, header) == false)
	{
		return false;
	}

	if (header.GetNalUnitType() != H265NALUnitType::VPS)
	{
		return false;
	}

	(void)H265_READ_BITS(vps.vps_video_parameter_set_id, 4);
	uint8_t H265_READ_BITS(vps_base_layer_internal_flag, 1);
	uint8_t H265_READ_BITS(vps_base_layer_available_flag, 1);
	uint8_t H265_READ_BITS(vps_max_layers_minus1, 6);
	uint8_t H265_READ_BITS(vps_max_sub_layers_minus1, 3);
	uint8_t H265_READ_BITS(vps_temporal_id_nesting_flag, 1);
	uint16_t H265_READ_BITS(vps_reserved_0xffff_16bits, 16);
	// profile_tier_level(1, vps_max_sub_layers_minus1);
	ProfileTierLevel profile_tier_level;
	if (ProcessProfileTierLevel(vps_max_sub_layers_minus1, parser, profile_tier_level) == false)
	{
		return false;
	}
	uint8_t H265_READ_BITS(vps_sub_layer_ordering_info_present_flag, 1);

	for (size_t i = (vps_sub_layer_ordering_info_present_flag ? 0 : vps_max_sub_layers_minus1); i <= vps_max_sub_layers_minus1; i++)
	{
		// vps_max_dec_pic_buffering_minus1[ i ] ue(v)
		uint32_t H265_READ_UEV(vps_max_dec_pic_buffering_minus1);
		// vps_max_num_reorder_pics[ i ] ue(v)
		uint32_t H265_READ_UEV(vps_max_num_reorder_pics);
		// vps_max_latency_increase_plus1[ i ] ue(v)
		uint32_t H265_READ_UEV(vps_max_latency_increase_plus1);
	}

	uint8_t H265_READ_BITS(vps_max_layer_id, 6);
	uint32_t H265_READ_UEV(vps_num_layer_sets_minus1);
	for (size_t i = 1; i <= vps_num_layer_sets_minus1; i++)
	{
		for (size_t j = 0; j <= vps_max_layer_id; j++)
		{
			// layer_id_included_flag[ i ][ j ] u(1)
			uint8_t H265_READ_BITS(layer_id_included_flag, 1);
		}
	}

	uint8_t H265_READ_BITS(vps_timing_info_present_flag, 1);
	if (vps_timing_info_present_flag)
	{
		uint32_t H265_READ_BITS(vps_num_units_in_tick, 32);
		uint32_t H265_READ_BITS(vps_time_scale, 32);
		uint8_t H265_READ_BITS(vps_poc_proportional_to_timing_flag, 1);
		if (vps_poc_proportional_to_timing_flag)
		{
			uint32_t H265_READ_UEV(vps_num_ticks_poc_diff_one_minus1);
		}
		uint32_t H265_READ_UEV(vps_num_hrd_parameters);
		for (size_t i = 0; i < vps_num_hrd_parameters; i++)
		{
			uint32_t H265_READ_UEV(hrd_layer_set_idx[i]);
			if (i > 0)
			{
				uint8_t H265_READ_BITS(cprms_present_flag[i], 1);
			}

			// hrd_parameters(cprms_present_flag[i], vps_max_sub_layers_minus1)
			HrdParameters hrd_params;
			if (ProcessHrdParameters(1, vps_max_sub_layers_minus1, parser, hrd_params) == false)
			{
				return false;
			}
		}
	}

	// uint8_t H265_READ_BITS(vps_extension_flag, 1);
	// if( vps_extension_flag )
	// {
	// 	while( more_rbsp_data( ) )
	// 	{
	// 		uint8_t H265_READ_BITS(vps_extension_data_flag, 1);
	// 	}
	// }

	// rbsp_trailing_bits( )

	return true;
}

bool H265Parser::ParseSPS(const uint8_t *nalu, size_t length, H265SPS &sps)
{
	NalUnitBitstreamParser parser(nalu, length);

	H265NalUnitHeader header;

	if (ParseNalUnitHeader(parser, header) == false)
	{
		return false;
	}

	if (header.GetNalUnitType() != H265NALUnitType::SPS)
	{
		return false;
	}

	///////////////////////
	// SPS
	///////////////////////
	uint8_t sps_video_parameter_set_id;
	if (parser.ReadBits(4, sps_video_parameter_set_id) == false)
	{
		return false;
	}

	uint8_t max_sub_layers_minus1;
	if (parser.ReadBits(3, max_sub_layers_minus1) == false)
	{
		return false;
	}

	sps._max_sub_layers_minus1 = max_sub_layers_minus1;

	uint8_t temporal_id_nesting_flag;
	if (parser.ReadBits(1, temporal_id_nesting_flag) == false)
	{
		return false;
	}

	sps._temporal_id_nesting_flag = temporal_id_nesting_flag;

	ProfileTierLevel profile_tier_level;
	if (ProcessProfileTierLevel(max_sub_layers_minus1, parser, profile_tier_level) == false)
	{
		return false;
	}

	sps._profile_tier_level = profile_tier_level;

	uint32_t sps_seq_parameter_set_id;
	if (parser.ReadUEV(sps_seq_parameter_set_id) == false)
	{
		return false;
	}

	uint32_t chroma_format_idc;
	if (parser.ReadUEV(chroma_format_idc) == false)
	{
		return false;
	}
	
	// 0 - 4:0:0
	// 1 - 4:2:0
	// 2 - 4:2:2
	// 3 - 4:4:4
	if (chroma_format_idc > 3)
	{
		logte("Invalid chroma_format_idc parsed: %u", chroma_format_idc);
		return false;
	}

	sps._chroma_format_idc = chroma_format_idc;

	uint8_t separate_colour_plane_flag = 0;
	if (chroma_format_idc == 3)
	{
		if (parser.ReadBits(1, separate_colour_plane_flag) == false)
		{
			return false;
		}
	}

	uint32_t pic_width_in_luma_samples;
	if (parser.ReadUEV(pic_width_in_luma_samples) == false)
	{
		return false;
	}

	uint32_t pic_height_in_luma_samples;
	if (parser.ReadUEV(pic_height_in_luma_samples) == false)
	{
		return false;
	}

	uint8_t conformance_window_flag;
	if (parser.ReadBits(1, conformance_window_flag) == false)
	{
		return false;
	}

	uint32_t crop_x = 0, crop_y = 0;
	uint32_t conf_win_left_offset, conf_win_right_offset, conf_win_top_offset, conf_win_bottom_offset;
	if (conformance_window_flag)
	{
		if (parser.ReadUEV(conf_win_left_offset) == false)
		{
			return false;
		}

		if (parser.ReadUEV(conf_win_right_offset) == false)
		{
			return false;
		}

		if (parser.ReadUEV(conf_win_top_offset) == false)
		{
			return false;
		}

		if (parser.ReadUEV(conf_win_bottom_offset) == false)
		{
			return false;
		}

		// 0 - 4:0:0
		// 1 - 4:2:0
		// 2 - 4:2:2
		// 3 - 4:4:4
		int sub_width_c = 1, sub_height_c = 1;
		if (chroma_format_idc == 1)
		{
			sub_width_c = 2;
			sub_height_c = 2;
		}
		else if (chroma_format_idc == 2)
		{
			sub_width_c = 2;
			sub_height_c = 1;
		}

		crop_x = sub_width_c * (conf_win_left_offset + conf_win_right_offset);
		crop_y = sub_height_c * (conf_win_top_offset + conf_win_bottom_offset);
	}

	int64_t coded_width	   = pic_width_in_luma_samples;
	int64_t coded_height   = pic_height_in_luma_samples;
	int64_t display_width  = pic_width_in_luma_samples - crop_x;
	int64_t display_height = pic_height_in_luma_samples - crop_y;

	// Validate: Check negative values
	if (display_width < 0 || display_height < 0)
	{
		return false;
	}

	// Validate: Check maximum width and height (8K, 8192x4320)
	if (display_width > 8192 || display_height > 8192)
	{
		return false;
	}

	// Validate: Check total pixels
	if (display_width * display_height > 8192 * 4320)
	{
		return false;
	}

	logtt("Parsed SPS resolution: coded(%lld x %lld), crop(%lld x %lld), display(%lld x %lld)",
		  coded_width, coded_height,
		  crop_x, crop_y,
		  display_width, display_height);

	sps._width	= static_cast<uint32_t>(display_width);
	sps._height = static_cast<uint32_t>(display_height);
		  
	uint32_t bit_depth_luma_minus8;
	if (parser.ReadUEV(bit_depth_luma_minus8) == false)
	{
		return false;
	}
	sps._bit_depth_luma_minus8 = bit_depth_luma_minus8;

	uint32_t bit_depth_chroma_minus8;
	if (parser.ReadUEV(bit_depth_chroma_minus8) == false)
	{
		return false;
	}
	sps._bit_depth_chroma_minus8 = bit_depth_chroma_minus8;

	uint32_t log2_max_pic_order_cnt_lsb_minus4;
	if (parser.ReadUEV(log2_max_pic_order_cnt_lsb_minus4) == false)
	{
		return false;
	}

	uint8_t sps_sub_layer_ordering_info_present_flag;
	if (parser.ReadBits(1, sps_sub_layer_ordering_info_present_flag) == false)
	{
		return false;
	}

	for (int i = (sps_sub_layer_ordering_info_present_flag ? 0 : max_sub_layers_minus1); i <= max_sub_layers_minus1; i++)
	{
		uint32_t sps_max_dec_pic_buffering_minus1;
		if (parser.ReadUEV(sps_max_dec_pic_buffering_minus1) == false)
		{
			return false;
		}

		uint32_t sps_max_num_reorder_pics;
		if (parser.ReadUEV(sps_max_num_reorder_pics) == false)
		{
			return false;
		}

		uint32_t sps_max_latency_increase_plus1;
		if (parser.ReadUEV(sps_max_latency_increase_plus1) == false)
		{
			return false;
		}
	}

	uint32_t log2_min_luma_coding_block_size_minus3;
	if (parser.ReadUEV(log2_min_luma_coding_block_size_minus3) == false)
	{
		return false;
	}

	uint32_t log2_diff_max_min_luma_coding_block_size;
	if (parser.ReadUEV(log2_diff_max_min_luma_coding_block_size) == false)
	{
		return false;
	}

	uint32_t log2_min_transform_block_size_minus2;
	if (parser.ReadUEV(log2_min_transform_block_size_minus2) == false)
	{
		return false;
	}

	uint32_t log2_diff_max_min_transform_block_size;
	if (parser.ReadUEV(log2_diff_max_min_transform_block_size) == false)
	{
		return false;
	}

	uint32_t max_transform_hierarchy_depth_inter;
	if (parser.ReadUEV(max_transform_hierarchy_depth_inter) == false)
	{
		return false;
	}

	uint32_t max_transform_hierarchy_depth_intra;
	if (parser.ReadUEV(max_transform_hierarchy_depth_intra) == false)
	{
		return false;
	}

	uint8_t scaling_list_enabled_flag;
	if (parser.ReadBits(1, scaling_list_enabled_flag) == false)
	{
		return false;
	}

	if (scaling_list_enabled_flag == 1)
	{
		uint8_t sps_scaling_list_data_present_flag;
		if (parser.ReadBits(1, sps_scaling_list_data_present_flag) == false)
		{
			return false;
		}

		if (sps_scaling_list_data_present_flag == 1)
		{
			// Scaling List Data
			for (int i = 0; i < 4; i++)
			{
				for (int j = 0; j < (i == 3 ? 2 : 6); j++)
				{
					uint8_t scaling_list_pred_mode_flag;
					if (parser.ReadBits(1, scaling_list_pred_mode_flag) == false)
					{
						return false;
					}

					if (scaling_list_pred_mode_flag == 0)
					{
						uint32_t scaling_list_pred_matrix_id_delta;
						if (parser.ReadUEV(scaling_list_pred_matrix_id_delta) == false)
						{
							return false;
						}
					}
					else
					{
						uint32_t coef_num = std::min(64, (1 << (4 + (i << 1))));
						if (i > 1)
						{
							int32_t scaling_list_dc_coef_minus8;
							if (parser.ReadSEV(scaling_list_dc_coef_minus8) == false)
							{
								return false;
							}

							for (uint32_t k = 0; k < coef_num; k++)
							{
								int32_t scaling_list_delta_coef;
								if (parser.ReadSEV(scaling_list_delta_coef) == false)
								{
									return false;
								}
							}
						}
					}
				}
			}
		}
	}

	uint8_t amp_enabled_flag;
	if (parser.ReadBits(1, amp_enabled_flag) == false)
	{
		return false;
	}

	uint8_t sample_adaptive_offset_enabled_flag;
	if (parser.ReadBits(1, sample_adaptive_offset_enabled_flag) == false)
	{
		return false;
	}

	uint8_t pcm_enabled_flag;
	if (parser.ReadBits(1, pcm_enabled_flag) == false)
	{
		return false;
	}

	if (pcm_enabled_flag == 1)
	{
		uint8_t pcm_sample_bit_depth_luma_minus1;
		if (parser.ReadBits(4, pcm_sample_bit_depth_luma_minus1) == false)
		{
			return false;
		}

		uint8_t pcm_sample_bit_depth_chroma_minus1;
		if (parser.ReadBits(4, pcm_sample_bit_depth_chroma_minus1) == false)
		{
			return false;
		}

		uint32_t log2_min_pcm_luma_coding_block_size_minus3;
		if (parser.ReadUEV(log2_min_pcm_luma_coding_block_size_minus3) == false)
		{
			return false;
		}

		uint32_t log2_diff_max_min_pcm_luma_coding_block_size;
		if (parser.ReadUEV(log2_diff_max_min_pcm_luma_coding_block_size) == false)
		{
			return false;
		}

		uint8_t pcm_loop_filter_disabled_flag;
		if (parser.ReadBits(1, pcm_loop_filter_disabled_flag) == false)
		{
			return false;
		}
	}

	// Short term ref pic set

	uint32_t num_short_term_ref_pic_sets;
	if (parser.ReadUEV(num_short_term_ref_pic_sets) == false)
	{
		return false;
	}

	std::vector<ShortTermRefPicSet> rpset_list(num_short_term_ref_pic_sets);
	for (uint32_t i = 0; i < num_short_term_ref_pic_sets; i++)
	{
		if (ProcessShortTermRefPicSet(i, num_short_term_ref_pic_sets, rpset_list, parser, rpset_list[i]) == false)
		{
			return false;
		}
	}

	uint8_t long_term_ref_pics_present_flag;
	if (parser.ReadBits(1, long_term_ref_pics_present_flag) == false)
	{
		return false;
	}

	if (long_term_ref_pics_present_flag == 1)
	{
		uint32_t num_long_term_ref_pics_sps;
		if (parser.ReadUEV(num_long_term_ref_pics_sps) == false)
		{
			return false;
		}

		for (uint32_t i = 0; i < num_long_term_ref_pics_sps; i++)
		{
			uint32_t lt_ref_pic_poc_lsb_sps;
			if (parser.ReadBits(log2_max_pic_order_cnt_lsb_minus4 + 4, lt_ref_pic_poc_lsb_sps) == false)
			{
				return false;
			}

			uint8_t used_by_curr_pic_lt_sps_flag;
			if (parser.ReadBits(1, used_by_curr_pic_lt_sps_flag) == false)
			{
				return false;
			}
		}
	}

	uint8_t sps_temporal_mvp_enabled_flag;
	if (parser.ReadBits(1, sps_temporal_mvp_enabled_flag) == false)
	{
		return false;
	}
	uint8_t strong_intra_smoothing_enabled_flag;
	if (parser.ReadBits(1, strong_intra_smoothing_enabled_flag) == false)
	{
		return false;
	}
	uint8_t vui_parameters_present_flag;
	if (parser.ReadBits(1, vui_parameters_present_flag) == false)
	{
		return false;
	}

	if (vui_parameters_present_flag == 1)
	{
		VuiParameters params;
		if (ProcessVuiParameters(max_sub_layers_minus1, parser, params) == false)
		{
			return false;
		}

		sps._vui_parameters = params;
	}

	uint8_t sps_extension_flag;
	if (parser.ReadBits(1, sps_extension_flag) == false)
	{
		return false;
	}

	return true;
}

// Rec. ITU-T H.265 (V10), 7.3.2.3.2 Picture parameter set range extension syntax
bool ParsePpsRangeExtension(NalUnitBitstreamParser &parser, H265PPS &pps, uint8_t transform_skip_enabled_flag)
{
	if (transform_skip_enabled_flag)
	{
		uint32_t H265_READ_UEV(log2_max_transform_skip_block_size_minus2);
		uint8_t H265_READ_BITS(cross_component_prediction_enabled_flag, 1);
		uint8_t H265_READ_BITS(chroma_qp_offset_list_enabled_flag, 1);
		if (chroma_qp_offset_list_enabled_flag)
		{
			uint32_t H265_READ_UEV(diff_cu_chroma_qp_offset_depth);
			uint32_t H265_READ_UEV(chroma_qp_offset_list_len_minus1);
			for (uint32_t i = 0; i <= chroma_qp_offset_list_len_minus1; i++)
			{
				// cb_qp_offset_list[ i ]
				int32_t H265_READ_SEV(cb_qp_offset_list);
				// cr_qp_offset_list[ i ]
				int32_t H265_READ_SEV(cr_qp_offset_list);
			}
		}
		uint32_t H265_READ_UEV(log2_sao_offset_scale_luma);
		uint32_t H265_READ_UEV(log2_sao_offset_scale_chroma);
	}

	return true;
}

// Rec. ITU-T H.265 (V10), 7.3.2.3.3 Picture parameter set screen content coding extension syntax
bool ParsePpsSccExtension(NalUnitBitstreamParser &parser, H265PPS &pps)
{
	uint8_t H265_READ_BITS(pps_curr_pic_ref_enabled_flag, 1);
	uint8_t H265_READ_BITS(residual_adaptive_colour_transform_enabled_flag, 1);
	if (residual_adaptive_colour_transform_enabled_flag)
	{
		uint8_t H265_READ_BITS(pps_slice_act_qp_offsets_present_flag, 1);
		int32_t H265_READ_SEV(pps_act_y_qp_offset_plus5);
		int32_t H265_READ_SEV(pps_act_cb_qp_offset_plus5);
		int32_t H265_READ_SEV(pps_act_cr_qp_offset_plus3);
	}
	uint8_t H265_READ_BITS(pps_palette_predictor_initializers_present_flag, 1);
	if (pps_palette_predictor_initializers_present_flag)
	{
		uint32_t H265_READ_UEV(pps_num_palette_predictor_initializers);
		if (pps_num_palette_predictor_initializers > 0)
		{
			uint8_t H265_READ_BITS(monochrome_palette_flag, 1);
			uint32_t H265_READ_UEV(luma_bit_depth_entry_minus8);
			uint32_t chroma_bit_depth_entry_minus8 = 0;
			if (!monochrome_palette_flag)
			{
				uint32_t H265_READ_UEV(chroma_bit_depth_entry_minus8);
			}
			int num_comps = monochrome_palette_flag ? 1 : 3;
			for (int comp = 0; comp < num_comps; comp++)
			{
				for (uint32_t i = 0; i < pps_num_palette_predictor_initializers; i++)
				{
					// pps_palette_predictor_initializer[comp][i] u(v)

					// pps_palette_predictor_initializer[ comp ][ i ] specifies the value of the comp-th component of the i-th palette entry in
					// the PPS that is used to initialize the array PredictorPaletteEntries. For values of i in the range of 0 to
					// pps_num_palette_predictor_initializers − 1, inclusive, the number of bits used to represent
					// pps_palette_predictor_initializer[ 0 ][ i ] is luma_bit_depth_entry_minus8 + 8, and the number of bits used to represent
					// pps_palette_predictor_initializer[ 1 ][ i ] and pps_palette_predictor_initializer[ 2 ][ i ] is chroma_bit_depth_entry_
					// minus8 + 8.
					uint32_t H265_READ_BITS(pps_palette_predictor_initializer,
								   (comp == 0)
									   ? luma_bit_depth_entry_minus8 + 8
									   : chroma_bit_depth_entry_minus8 + 8);
				}
			}
		}
	}

	return true;
}

// Rec. ITU-T H.265 (V10), F.7.3.2.3.6 Colour mapping octants syntax
bool ParseColourMappingOctants(NalUnitBitstreamParser &parser, H265PPS &pps,
							   uint8_t cm_octant_depth,
							   uint8_t cm_y_part_num_log2,
							   uint32_t luma_bit_depth_cm_input_minus8,
							   uint32_t luma_bit_depth_cm_output_minus8,
							   uint8_t cm_res_quant_bits,
							   uint8_t cm_delta_flc_bits_minus1,
							   int inpDepth, int idxY, int idxCb, int idxCr, int inpLength)
{
	// cm_octant_depth specifies the maximal split depth of the colour mapping table. In bitstreams conforming to this version
	// of this Specification, the value of cm_octant_depth shall be in the range of 0 to 1, inclusive. Other values for
	// cm_octant_depth are reserved for future use by ITU-T | ISO/IEC.
	// The variable OctantNumC is derived as follows:
	// OctantNumC = 1 << cm_octant_depth
	[[maybe_unused]] const uint32_t OctantNumC = 1 << cm_octant_depth;
	// PartNumY = 1 << cm_y_part_num_log2 (F-43)
	const uint32_t PartNumY = 1 << cm_y_part_num_log2;

	// BitDepthCmOutputY = 8 + luma_bit_depth_cm_output_minus8 (F-46)
	const uint32_t BitDepthCmOutputY = 8 + luma_bit_depth_cm_output_minus8;
	// BitDepthCmInputY = 8 + luma_bit_depth_cm_input_minus8 (F-44)
	const uint32_t BitDepthCmInputY = 8 + luma_bit_depth_cm_input_minus8;
	// cm_delta_flc_bits_minus1 specifies the delta value used to derive the number of bits used to code the syntax element
	// res_coeff_r. The variable CMResLSBits is set equal to
	// Max( 0, ( 10 + BitDepthCmInputY − BitDepthCmOutputY − cm_res_quant_bits − ( cm_delta_flc_bits_minus1 + 1 ) ) ).
	const uint32_t CMResLSBits = std::max(0U, (10 + BitDepthCmInputY - BitDepthCmOutputY - cm_res_quant_bits - (cm_delta_flc_bits_minus1 + 1)));

	uint8_t split_octant_flag = 0;

	if (inpDepth < cm_octant_depth)
	{
		(void)H265_READ_BITS(split_octant_flag, 1);
	}

	if (split_octant_flag)
	{
		for (int k = 0; k < 2; k++)
		{
			for (int m = 0; m < 2; m++)
			{
				for (int n = 0; n < 2; n++)
				{
					// colour_mapping_octants( inpDepth + 1, idxY + PartNumY * k * inpLength / 2,
					// idxCb + m * inpLength / 2, idxCr + n * inpLength / 2, inpLength / 2 );
					ParseColourMappingOctants(parser, pps,
											  cm_octant_depth,
											  cm_y_part_num_log2,
											  luma_bit_depth_cm_input_minus8,
											  luma_bit_depth_cm_output_minus8,
											  cm_res_quant_bits,
											  cm_delta_flc_bits_minus1,
											  inpDepth + 1, idxY + PartNumY * k * inpLength / 2, idxCb + m * inpLength / 2, idxCr + n * inpLength / 2, inpLength / 2);
				}
			}
		}
	}
	else
	{
		for (uint32_t i = 0; i < PartNumY; i++)
		{
			[[maybe_unused]] auto idxShiftY = idxY + (i << (cm_octant_depth - inpDepth));
			for (int j = 0; j < 4; j++)
			{
				// coded_res_flag[ idxShiftY ][ idxCb ][ idxCr ][ j ] u(1)
				uint8_t H265_READ_BITS(coded_res_flag, 1);

				if (coded_res_flag)
				{
					for (int c = 0; c < 3; c++)
					{
						// res_coeff_q[ idxShiftY ][ idxCb ][ idxCr ][ j ][ c ] ue(v)
						uint32_t H265_READ_UEV(res_coeff_q);

						// res_coeff_r[ idxShiftY ][ idxCb ][ idxCr ][ j ][ c ] u(v)

						// res_coeff_r[ idxShiftY ][ idxCb ][ idxCr ][ j ][ c ] specifies the remainder of the residual for the j-th colour mapping
						// coefficient of the c-th colour component of the octant with octant index equal to ( idxShiftY, idxCb, idxCr ). The number
						// of bits used to code res_coeff_r is equal to CMResLSBits. If CMResLSBits is equal to 0, res_coeff_r is not present. When
						// not present, the value of res_coeff_r[ idxShiftY ][ idxCb ][ idxCr ][ j ][ c ] is inferred to be equal to 0.
						uint32_t res_coeff_r = 0;
						if (CMResLSBits != 0)
						{
							(void)H265_READ_BITS(res_coeff_r, CMResLSBits);
						}

						// if( res_coeff_q[ idxShiftY ][ idxCb ][ idxCr ][ j ][ c ] ||
						//	res_coeff_r[ idxShiftY ][ idxCb ][ idxCr ][ j ][ c ] )
						if ((res_coeff_q != 0) || (res_coeff_r != 0))
						{
							// res_coeff_s[ idxShiftY ][ idxCb ][ idxCr ][ j ][ c ] u(1)
							uint8_t H265_READ_BITS(res_coeff_s, 1);
						}
					}
				}
			}
		}
	}

	return true;
}

// Rec. ITU-T H.265 (V10), F.7.3.2.3.5 General colour mapping table syntax
bool ParseColourMappingTable(NalUnitBitstreamParser &parser, H265PPS &pps)
{
	uint32_t H265_READ_UEV(num_cm_ref_layers_minus1);
	for (uint32_t i = 0; i <= num_cm_ref_layers_minus1; i++)
	{
		// cm_ref_layer_id[ i ] u(6)
		uint8_t H265_READ_BITS(cm_ref_layer_id, 6);
	}
	uint8_t H265_READ_BITS(cm_octant_depth, 2);
	uint8_t H265_READ_BITS(cm_y_part_num_log2, 2);
	uint32_t H265_READ_UEV(luma_bit_depth_cm_input_minus8);
	uint32_t H265_READ_UEV(chroma_bit_depth_cm_input_minus8);
	uint32_t H265_READ_UEV(luma_bit_depth_cm_output_minus8);
	uint32_t H265_READ_UEV(chroma_bit_depth_cm_output_minus8);
	uint8_t H265_READ_BITS(cm_res_quant_bits, 2);
	uint8_t H265_READ_BITS(cm_delta_flc_bits_minus1, 2);
	if (cm_octant_depth == 1)
	{
		int32_t H265_READ_SEV(cm_adapt_threshold_u_delta);
		int32_t H265_READ_SEV(cm_adapt_threshold_v_delta);
	}

	// colour_mapping_octants(0, 0, 0, 0, 1 << cm_octant_depth);
	ParseColourMappingOctants(parser, pps,
							  cm_octant_depth,
							  cm_y_part_num_log2,
							  luma_bit_depth_cm_input_minus8,
							  luma_bit_depth_cm_output_minus8,
							  cm_res_quant_bits,
							  cm_delta_flc_bits_minus1,
							  0, 0, 0, 0, 1 << cm_octant_depth);

	return true;
}

// Rec. ITU-T H.265 (V10), F.7.3.2.3.4 Picture parameter set multilayer extension syntax
bool ParsePpsMultilayerExtension(NalUnitBitstreamParser &parser, H265PPS &pps)
{
	uint8_t H265_READ_BITS(poc_reset_info_present_flag, 1);
	uint8_t H265_READ_BITS(pps_infer_scaling_list_flag, 1);
	if (pps_infer_scaling_list_flag)
	{
		uint8_t H265_READ_BITS(pps_scaling_list_ref_layer_id, 6);
	}
	uint32_t H265_READ_UEV(num_ref_loc_offsets);
	for (uint32_t i = 0; i < num_ref_loc_offsets; i++)
	{
		// ref_loc_offset_layer_id[i]
		uint8_t H265_READ_BITS(ref_loc_offset_layer_id, 6);
		// scaled_ref_layer_offset_present_flag[i]
		uint8_t H265_READ_BITS(scaled_ref_layer_offset_present_flag, 1);
		if (scaled_ref_layer_offset_present_flag)
		{
			// scaled_ref_layer_left_offset[ref_loc_offset_layer_id[i]]
			int32_t H265_READ_SEV(scaled_ref_layer_left_offset);
			// scaled_ref_layer_top_offset[ref_loc_offset_layer_id[i]]
			int32_t H265_READ_SEV(scaled_ref_layer_top_offset);
			// scaled_ref_layer_right_offset[ref_loc_offset_layer_id[i]]
			int32_t H265_READ_SEV(scaled_ref_layer_right_offset);
			// scaled_ref_layer_bottom_offset[ref_loc_offset_layer_id[i]]
			int32_t H265_READ_SEV(scaled_ref_layer_bottom_offset);
		}
		// ref_region_offset_present_flag[i]
		uint8_t H265_READ_BITS(ref_region_offset_present_flag, 1);
		if (ref_region_offset_present_flag)
		{
			// ref_region_left_offset[ref_loc_offset_layer_id[i]]
			int32_t H265_READ_SEV(ref_region_left_offset);
			// ref_region_top_offset[ref_loc_offset_layer_id[i]]
			int32_t H265_READ_SEV(ref_region_top_offset);
			// ref_region_right_offset[ref_loc_offset_layer_id[i]]
			int32_t H265_READ_SEV(ref_region_right_offset);
			// ref_region_bottom_offset[ref_loc_offset_layer_id[i]]
			int32_t H265_READ_SEV(ref_region_bottom_offset);
		}
		// resample_phase_set_present_flag[i]
		uint8_t H265_READ_BITS(resample_phase_set_present_flag, 1);
		if (resample_phase_set_present_flag)
		{
			// phase_hor_luma[ref_loc_offset_layer_id[i]]
			uint32_t H265_READ_UEV(phase_hor_luma);
			// phase_ver_luma[ref_loc_offset_layer_id[i]]
			uint32_t H265_READ_UEV(phase_ver_luma);
			// phase_hor_chroma_plus8[ref_loc_offset_layer_id[i]]
			uint32_t H265_READ_UEV(phase_hor_chroma_plus8);
			// phase_ver_chroma_plus8[ref_loc_offset_layer_id[i]]
			uint32_t H265_READ_UEV(phase_ver_chroma_plus8);
		}
	}
	uint8_t H265_READ_BITS(colour_mapping_enabled_flag, 1);
	if (colour_mapping_enabled_flag)
	{
		// colour_mapping_table();
		return ParseColourMappingTable(parser, pps);
	}

	return true;
}

// Rec. ITU-T H.265 (V10), I.7.3.2.3.7 Picture parameter set 3D extension syntax
bool ParsePps3dExtension(NalUnitBitstreamParser &parser, H265PPS &pps)
{
	uint8_t H265_READ_BITS(dlts_present_flag, 1);

	if (dlts_present_flag)
	{
		uint8_t H265_READ_BITS(pps_depth_layers_minus1, 6);
		uint8_t H265_READ_BITS(pps_bit_depth_for_depth_layers_minus8, 4);

		for (uint8_t i = 0; i <= pps_depth_layers_minus1; i++)
		{
			// dlt_flag[i]
			uint8_t H265_READ_BITS(dlt_flag, 1);

			if (dlt_flag)
			{
				// dlt_pred_flag[i]
				uint8_t H265_READ_BITS(dlt_pred_flag, 1);
				// dlt_val_flags_present_flag[i]
				uint8_t dlt_val_flags_present_flag = 0;
				if (dlt_pred_flag == 0)
				{
					uint8_t H265_READ_BITS(dlt_val_flags_present_flag, 1);
				}
				if (dlt_val_flags_present_flag)
				{
					// The variable depthMaxValue is set equal to ( 1 << ( pps_bit_depth_for_depth_layers_minus8 + 8 ) ) − 1.
					uint32_t depth_max_value = (1 << (pps_bit_depth_for_depth_layers_minus8 + 8)) - 1;
					for (uint32_t j = 0; j <= depth_max_value; j++)
					{
						uint8_t H265_READ_BITS(dlt_value_flag[i][j], 1);
					}
				}
				else
				{
					// delta_dlt(i)
					// Rec. ITU-T H.265 (V10), I.7.3.2.3.8 Delta depth look-up table syntax

					// num_val_delta_dlt specifies the number of elements in the list deltaList. The length of num_val_delta_dlt syntax element
					// is pps_bit_depth_for_depth_layers_minus8 + 8 bits.
					uint32_t H265_READ_BITS(num_val_delta_dlt, pps_bit_depth_for_depth_layers_minus8 + 8);
					if (num_val_delta_dlt > 0)
					{
						uint32_t max_diff = 0;
						if (num_val_delta_dlt > 1)
						{
							// max_diff specifies the maximum difference between two consecutive elements in the list deltaList. The length of
							// max_diff syntax element is pps_bit_depth_for_depth_layers_minus8 + 8 bits. When not present, the value of max_diff is
							// inferred to be equal to 0.
							(void)H265_READ_BITS(max_diff, pps_bit_depth_for_depth_layers_minus8 + 8);
						}

						int32_t min_diff_minus1 = max_diff - 1;
						if (num_val_delta_dlt > 2 && max_diff > 0)
						{
							// min_diff_minus1 specifies the minimum difference between two consecutive elements in the list deltaList.
							// min_diff_minus1 shall be in the range of 0 to max_diff − 1, inclusive. The length of the min_diff_minus1 syntax element
							// is Ceil( Log2( max_diff + 1 ) ) bits. When not present, the value of min_diff_minus1 is inferred to be equal to
							// ( max_diff − 1 ).
							uint32_t H265_READ_BITS(min_diff_minus1, std::ceil(std::log2(max_diff + 1)));
						}

						// delta_dlt_val0 specifies the 0-th element in the list deltaList. The length of the delta_dlt_val0 syntax element is
						// pps_bit_depth_for_depth_layers_minus8 + 8 bits
						uint32_t H265_READ_BITS(delta_dlt_val0, pps_bit_depth_for_depth_layers_minus8 + 8);

						if (max_diff > static_cast<uint32_t>(min_diff_minus1 + 1))
						{
							auto min_diff = min_diff_minus1 + 1;
							for (uint32_t k = 1; k < num_val_delta_dlt; k++)
							{
								// delta_val_diff_minus_min[ k ] plus minDiff specifies the difference between the k-th element and the ( k − 1 )-th
								// element in the list deltaList. The length of delta_val_diff_minus_min[ k ] syntax element is
								// Ceil( Log2( max_diff − minDiff + 1 ) ) bits. When not present, the value of delta_val_diff_minus_min[ k ] is inferred to
								// be equal to 0.
								// delta_val_diff_minus_min[ k ] u(v)
								uint32_t H265_READ_BITS(delta_val_diff_minus_min, std::ceil(std::log2(max_diff - min_diff + 1)));
							}
						}
					}
				}
			}
		}
	}

	return true;
}

// Rec. ITU-T H.265 (V10), 7.3.4 Scaling list data syntax
bool ParseScalingListData(NalUnitBitstreamParser &parser, H265PPS &pps)
{
	for(int size_id = 0; size_id < 4; size_id++)
	{
		for (int matrix_id = 0; matrix_id < (size_id == 3 ? 2 : 6); matrix_id++)
		{
			uint8_t H265_READ_BITS(scaling_list_pred_mode_flag, 1);

			if (scaling_list_pred_mode_flag == 0)
			{
				uint32_t H265_READ_UEV(scaling_list_pred_matrix_id_delta);
			}
			else
			{
				// nextCoef = 8

				uint32_t coef_num = std::min(64, (1 << (4 + (size_id << 1))));
				if (size_id > 1)
				{
					int32_t H265_READ_SEV(scaling_list_dc_coef_minus8);
					// nextCoef = scaling_list_dc_coef_minus8[ sizeId − 2 ][ matrixId ] + 8
				}

				for (uint32_t i = 0; i < coef_num; i++)
				{
					int32_t H265_READ_SEV(scaling_list_delta_coef);
					// nextCoef = ( nextCoef + scaling_list_delta_coef + 256 ) % 256
					// ScalingList[sizeId][MatrixId][i] = nextCoef;
				}
			}
		}
	}

	return true;
}

// Rec. ITU-T H.265 (V10), 7.3.2.3 Picture parameter set RBSP syntax
bool H265Parser::ParsePPS(const uint8_t *nalu, size_t length, H265PPS &pps)
{
	NalUnitBitstreamParser parser(nalu, length);

	H265NalUnitHeader header;

	if (ParseNalUnitHeader(parser, header) == false)
	{
		return false;
	}

	if (header.GetNalUnitType() != H265NALUnitType::PPS)
	{
		return false;
	}

	(void)H265_READ_UEV(pps.pps_pic_parameter_set_id);
	(void)H265_READ_UEV(pps.pps_seq_parameter_set_id);
	uint8_t H265_READ_BITS(dependent_slice_segments_enabled_flag, 1);
	uint8_t H265_READ_BITS(output_flag_present_flag, 1);
	uint8_t H265_READ_BITS(num_extra_slice_header_bits, 3);
	uint8_t H265_READ_BITS(sign_data_hiding_enabled_flag, 1);
	uint8_t H265_READ_BITS(cabac_init_present_flag, 1);
	uint32_t H265_READ_UEV(num_ref_idx_l0_default_active_minus1);
	uint32_t H265_READ_UEV(num_ref_idx_l1_default_active_minus1);
	int32_t H265_READ_SEV(init_qp_minus26);
	uint8_t H265_READ_BITS(constrained_intra_pred_flag, 1);
	uint8_t H265_READ_BITS(transform_skip_enabled_flag, 1);
	uint8_t H265_READ_BITS(cu_qp_delta_enabled_flag, 1);
	if (cu_qp_delta_enabled_flag)
	{
		uint32_t H265_READ_UEV(diff_cu_qp_delta_depth);
	}
	int32_t H265_READ_SEV(pps_cb_qp_offset);
	int32_t H265_READ_SEV(pps_cr_qp_offset);
	uint8_t H265_READ_BITS(pps_slice_chroma_qp_offsets_present_flag, 1);
	uint8_t H265_READ_BITS(weighted_pred_flag, 1);
	uint8_t H265_READ_BITS(weighted_bipred_flag, 1);
	uint8_t H265_READ_BITS(transquant_bypass_enabled_flag, 1);
	uint8_t H265_READ_BITS(tiles_enabled_flag, 1);
	uint8_t H265_READ_BITS(entropy_coding_sync_enabled_flag, 1);
	if (tiles_enabled_flag)
	{
		uint32_t H265_READ_UEV(num_tile_columns_minus1);
		uint32_t H265_READ_UEV(num_tile_rows_minus1);
		uint8_t H265_READ_BITS(uniform_spacing_flag, 1);
		if (uniform_spacing_flag == 0)
		{
			for (uint32_t i = 0; i < num_tile_columns_minus1; i++)
			{
				uint32_t H265_READ_UEV(column_width_minus1[i]);
			}
			for (uint32_t i = 0; i < num_tile_rows_minus1; i++)
			{
				uint32_t H265_READ_UEV(row_height_minus1[i]);
			}
		}
		uint8_t H265_READ_BITS(loop_filter_across_tiles_enabled_flag, 1);
	}
	uint8_t H265_READ_BITS(pps_loop_filter_across_slices_enabled_flag, 1);
	uint8_t H265_READ_BITS(deblocking_filter_control_present_flag, 1);
	if (deblocking_filter_control_present_flag)
	{
		uint8_t H265_READ_BITS(deblocking_filter_override_enabled_flag, 1);
		uint8_t H265_READ_BITS(pps_deblocking_filter_disabled_flag, 1);
		if (pps_deblocking_filter_disabled_flag == 0)
		{
			int32_t H265_READ_SEV(pps_beta_offset_div2);
			int32_t H265_READ_SEV(pps_tc_offset_div2);
		}
	}
	uint8_t H265_READ_BITS(pps_scaling_list_data_present_flag, 1);
	if (pps_scaling_list_data_present_flag)
	{
		if (ParseScalingListData(parser, pps) == false)
		{
			return false;
		}
	}
	uint8_t H265_READ_BITS(lists_modification_present_flag, 1);
	uint32_t H265_READ_UEV(log2_parallel_merge_level_minus2);
	uint8_t H265_READ_BITS(slice_segment_header_extension_present_flag, 1);
	uint8_t H265_READ_BITS(pps_extension_present_flag, 1);
	uint8_t pps_range_extension_flag = 0;
	uint8_t pps_multilayer_extension_flag = 0;
	uint8_t pps_3d_extension_flag = 0;
	uint8_t pps_scc_extension_flag = 0;
	uint8_t pps_extension_4bits = 0;
	if (pps_extension_present_flag)
	{
		(void)H265_READ_BITS(pps_range_extension_flag, 1);
		(void)H265_READ_BITS(pps_multilayer_extension_flag, 1);
		(void)H265_READ_BITS(pps_3d_extension_flag, 1);
		(void)H265_READ_BITS(pps_scc_extension_flag, 1);
		(void)H265_READ_BITS(pps_extension_4bits, 4);
	}
	if (pps_range_extension_flag)
	{
		// pps_range_extension()
		ParsePpsRangeExtension(parser, pps, transform_skip_enabled_flag);
	}
	if (pps_multilayer_extension_flag)
	{
		// pps_multilayer_extension()
		ParsePpsMultilayerExtension(parser, pps); /* specified in Annex F */
	}
	if (pps_3d_extension_flag)
	{
		// pps_3d_extension()
		ParsePps3dExtension(parser, pps); /* specified in Annex I */
	}
	if (pps_scc_extension_flag)
	{
		// pps_scc_extension()
		ParsePpsSccExtension(parser, pps);
	}
	if (pps_extension_4bits)
	{
		// while (more_rbsp_data())
		// {
		// 	uint8_t H265_READ_BITS(pps_extension_data_flag, 1);
		// }
	}
	// rbsp_trailing_bits()

	return true;
}

bool H265Parser::ProcessProfileTierLevel(uint32_t max_sub_layers_minus1, NalUnitBitstreamParser &parser, ProfileTierLevel &profile)
{
	uint8_t general_profile_space;
	if (parser.ReadBits(2, general_profile_space) == false)
	{
		return false;
	}
	profile._general_profile_space = general_profile_space;

	uint8_t general_tier_flag;
	if (parser.ReadBits(1, general_tier_flag) == false)
	{
		return false;
	}
	profile._general_tier_flag = general_tier_flag;

	uint8_t general_profile_idc;
	if (parser.ReadBits(5, general_profile_idc) == false)
	{
		return false;
	}
	profile._general_profile_idc = general_profile_idc;

	// general_profile_compatibility_flag
	uint32_t general_profile_compatibility_flags;
	if (parser.ReadBits(32, general_profile_compatibility_flags) == false)
	{
		return false;
	}

	profile._general_profile_compatibility_flags = general_profile_compatibility_flags;

	uint32_t general_constraint_indicator_flags_hi = 0;
  	uint16_t general_constraint_indicator_flags_lo = 0;

	if (parser.ReadBits(32, general_constraint_indicator_flags_hi) == false)
	{
		return false;
	}

	if (parser.ReadBits(16, general_constraint_indicator_flags_lo) == false)
	{
		return false;
	}

	profile._general_constraint_indicator_flags = general_constraint_indicator_flags_hi;
	profile._general_constraint_indicator_flags <<= 16;
	profile._general_constraint_indicator_flags |= general_constraint_indicator_flags_lo;

	// Belows are same as general constraint indicator flags (48bits)

	// uint8_t general_progressive_source_flag;
	// if (parser.ReadBits(1, general_progressive_source_flag) == false)
	// {
	// 	return false;
	// }

	// uint8_t general_interlaced_source_flag;
	// if (parser.ReadBits(1, general_interlaced_source_flag) == false)
	// {
	// 	return false;
	// }

	// uint8_t general_non_packed_constraint_flag;
	// if (parser.ReadBits(1, general_non_packed_constraint_flag) == false)
	// {
	// 	return false;
	// }

	// uint8_t general_frame_only_constraint_flag;
	// if (parser.ReadBits(1, general_frame_only_constraint_flag) == false)
	// {
	// 	return false;
	// }

	// if (parser.Skip(32) == false)
	// {
	// 	return false;
	// }

	// if (parser.Skip(12) == false)
	// {
	// 	return false;
	// }

	uint8_t general_level_idc;
	if (parser.ReadBits(8, general_level_idc) == false)
	{
		return false;
	}
	profile._general_level_idc = general_level_idc;

	std::vector<uint8_t> sub_layer_profile_present_flag_list;
	std::vector<uint8_t> sub_layer_level_present_flag_list;
	for (uint32_t i = 0; i < max_sub_layers_minus1; i++)
	{
		uint8_t sub_layer_profile_present_flag;
		if (parser.ReadBits(1, sub_layer_profile_present_flag) == false)
		{
			return false;
		}
		sub_layer_profile_present_flag_list.push_back(sub_layer_profile_present_flag);

		uint8_t sub_layer_level_present_flag;
		if (parser.ReadBits(1, sub_layer_level_present_flag) == false)
		{
			return false;
		}
		sub_layer_level_present_flag_list.push_back(sub_layer_level_present_flag);
	}

	if (max_sub_layers_minus1 > 0)
	{
		for (int i = max_sub_layers_minus1; i < 8; i++)
		{
			if (parser.Skip(2) == false)
			{
				return false;
			}
		}
	}

	for (uint32_t i = 0; i < max_sub_layers_minus1; i++)
	{
		if (sub_layer_profile_present_flag_list[i])
		{
			// sub_layer_profile_space - 2bits
			// sub_layer_tier_flag - 1 bit
			// sub_layer_profile_idc - 5 bits

			// sub_layer_profile_compatibility_flag - 32 bits

			// sub_layer_progressive_source_flag - 1 bit
			// sub_layer_interlaced_source_flag - 1 bit
			// sub_layer_non_packed_constraint_flag - 1 bit
			// sub_layer_frame_only_constraint_flag - 1 bit

			// 32 bits
			// 12 bits
			if (parser.Skip(88) == false)
			{
				return false;
			}
		}

		if (sub_layer_level_present_flag_list[i])
		{
			// sub_layer_level_idc
			if (parser.Skip(8) == false)
			{
				return false;
			}
		}
	}

	return true;
}

bool H265Parser::ProcessVuiParameters(uint32_t sps_max_sub_layers_minus1, NalUnitBitstreamParser &parser, VuiParameters &params)
{
	uint8_t aspect_ratio_info_present_flag;
	if (parser.ReadBits(1, aspect_ratio_info_present_flag) == false)
	{
		return false;
	}

	if (aspect_ratio_info_present_flag == 1)
	{
		uint8_t aspect_ratio_idc;
		if (parser.ReadBits(8, aspect_ratio_idc) == false)
		{
			return false;
		}
		params._aspect_ratio_idc = aspect_ratio_idc;

		// Extended SAR
		if (aspect_ratio_idc == 255)
		{
			uint16_t sar_width, sar_height;

			if (parser.ReadBits(16, sar_width) == false)
			{
				return false;
			}
			if (parser.ReadBits(16, sar_height) == false)
			{
				return false;
			}

			params._aspect_ratio._width = sar_width;
			params._aspect_ratio._height = sar_height;
		}
	}

	uint8_t overscan_info_present_flag;
	if (parser.ReadBits(1, overscan_info_present_flag) == false)
	{
		return false;
	}

	if (overscan_info_present_flag == 1)
	{
		uint8_t overscan_appropriate_flag;
		if (parser.ReadBits(1, overscan_appropriate_flag) == false)
		{
			return false;
		}
	}

	uint8_t video_signal_type_present_flag;
	if (parser.ReadBits(1, video_signal_type_present_flag) == false)
	{
		return false;
	}

	if (video_signal_type_present_flag == 1)
	{
		uint8_t video_format;
		if (parser.ReadBits(3, video_format) == false)
		{
			return false;
		}

		uint8_t video_full_range_flag;
		if (parser.ReadBits(1, video_full_range_flag) == false)
		{
			return false;
		}

		uint8_t colour_description_present_flag;
		if (parser.ReadBits(1, colour_description_present_flag) == false)
		{
			return false;
		}

		if (colour_description_present_flag == 1)
		{
			uint8_t colour_primaries;
			if (parser.ReadBits(8, colour_primaries) == false)
			{
				return false;
			}

			uint8_t transfer_characteristics;
			if (parser.ReadBits(8, transfer_characteristics) == false)
			{
				return false;
			}

			uint8_t matrix_coeffs;
			if (parser.ReadBits(8, matrix_coeffs) == false)
			{
				return false;
			}
		}
	}

	uint8_t chroma_loc_info_present_flag;
	if (parser.ReadBits(1, chroma_loc_info_present_flag) == false)
	{
		return false;
	}

	if (chroma_loc_info_present_flag == 1)
	{
		uint32_t chroma_sample_loc_type_top_field;
		if (parser.ReadUEV(chroma_sample_loc_type_top_field) == false)
		{
			return false;
		}

		uint32_t chroma_sample_loc_type_bottom_field;
		if (parser.ReadUEV(chroma_sample_loc_type_bottom_field) == false)
		{
			return false;
		}
	}

	uint8_t neutral_chroma_indication_flag;
	if (parser.ReadBits(1, neutral_chroma_indication_flag) == false)
	{
		return false;
	}

	uint8_t field_seq_flag;
	if (parser.ReadBits(1, field_seq_flag) == false)
	{
		return false;
	}

	uint8_t frame_field_info_present_flag;
	if (parser.ReadBits(1, frame_field_info_present_flag) == false)
	{
		return false;
	}

	uint8_t default_display_window_flag;
	if (parser.ReadBits(1, default_display_window_flag) == false)
	{
		return false;
	}

	if (default_display_window_flag == 1)
	{
		uint32_t def_disp_win_left_offset;
		if (parser.ReadUEV(def_disp_win_left_offset) == false)
		{
			return false;
		}
		uint32_t def_disp_win_right_offset;
		if (parser.ReadUEV(def_disp_win_right_offset) == false)
		{
			return false;
		}
		uint32_t def_disp_win_top_offset;
		if (parser.ReadUEV(def_disp_win_top_offset) == false)
		{
			return false;
		}
		uint32_t def_disp_win_bottom_offset;
		if (parser.ReadUEV(def_disp_win_bottom_offset) == false)
		{
			return false;
		}
	}

	uint8_t vui_timing_info_present_flag;
	if (parser.ReadBits(1, vui_timing_info_present_flag) == false)
	{
		return false;
	}

	if (vui_timing_info_present_flag == 1)
	{
		uint32_t vui_num_units_in_tick;
		if (parser.ReadBits(32, vui_num_units_in_tick) == false)
		{
			return false;
		}
		params._num_units_in_tick = vui_num_units_in_tick;

		uint32_t vui_time_scale;
		if (parser.ReadBits(32, vui_time_scale) == false)
		{
			return false;
		}
		params._time_scale = vui_time_scale;

		uint8_t vui_poc_proportional_to_timing_flag;
		if (parser.ReadBits(1, vui_poc_proportional_to_timing_flag) == false)
		{
			return false;
		}

		if (vui_poc_proportional_to_timing_flag == 1)
		{
			uint32_t vui_num_ticks_poc_diff_one_minus1;
			if (parser.ReadUEV(vui_num_ticks_poc_diff_one_minus1) == false)
			{
				return false;
			}
		}

		uint8_t vui_hrd_parameters_present_flag;
		if (parser.ReadBits(1, vui_hrd_parameters_present_flag) == false)
		{
			return false;
		}

		if (vui_hrd_parameters_present_flag == 1)
		{
			HrdParameters hrd_params;
			if (ProcessHrdParameters(1, sps_max_sub_layers_minus1, parser, hrd_params) == false)
			{
				return false;
			}
		}
	}

	uint8_t bitstream_restriction_flag;
	if (parser.ReadBits(1, bitstream_restriction_flag) == false)
	{
		return false;
	}

	if (bitstream_restriction_flag == 1)
	{
		uint8_t tiles_fixed_structure_flag;
		if (parser.ReadBits(1, tiles_fixed_structure_flag) == false)
		{
			return false;
		}

		uint8_t motion_vectors_over_pic_boundaries_flag;
		if (parser.ReadBits(1, motion_vectors_over_pic_boundaries_flag) == false)
		{
			return false;
		}

		uint8_t restricted_ref_pic_lists_flag;
		if (parser.ReadBits(1, restricted_ref_pic_lists_flag) == false)
		{
			return false;
		}

		uint32_t min_spatial_segmentation_idc;
		if (parser.ReadUEV(min_spatial_segmentation_idc) == false)
		{
			return false;
		}
		params._min_spatial_segmentation_idc = min_spatial_segmentation_idc;

		uint32_t max_bytes_per_pic_denom;
		if (parser.ReadUEV(max_bytes_per_pic_denom) == false)
		{
			return false;
		}

		uint32_t max_bits_per_min_cu_denom;
		if (parser.ReadUEV(max_bits_per_min_cu_denom) == false)
		{
			return false;
		}

		uint32_t log2_max_mv_length_horizontal;
		if (parser.ReadUEV(log2_max_mv_length_horizontal) == false)
		{
			return false;
		}

		uint32_t log2_max_mv_length_vertical;
		if (parser.ReadUEV(log2_max_mv_length_vertical) == false)
		{
			return false;
		}
	}

	return true;
}

bool H265Parser::ProcessHrdParameters(uint8_t common_inf_present_flag, uint32_t max_sub_layers_minus1, NalUnitBitstreamParser &parser, HrdParameters &params)
{
	uint8_t nal_hrd_parameters_present_flag = 0;
	uint8_t vcl_hrd_parameters_present_flag = 0;
	uint8_t sub_pic_hrd_params_present_flag = 0;
	if (common_inf_present_flag == 1)
	{
		if (parser.ReadBits(1, nal_hrd_parameters_present_flag) == false)
		{
			return false;
		}

		if (parser.ReadBits(1, vcl_hrd_parameters_present_flag) == false)
		{
			return false;
		}

		if (nal_hrd_parameters_present_flag == 1 || vcl_hrd_parameters_present_flag == 1)
		{
			if (parser.ReadBits(1, sub_pic_hrd_params_present_flag) == false)
			{
				return false;
			}

			if (sub_pic_hrd_params_present_flag == 1)
			{
				uint8_t tick_divisor_minus2;
				if (parser.ReadBits(8, tick_divisor_minus2) == false)
				{
					return false;
				}
				uint8_t du_cpb_removal_delay_increment_length_minus1;
				if (parser.ReadBits(5, du_cpb_removal_delay_increment_length_minus1) == false)
				{
					return false;
				}
				uint8_t sub_pic_cpb_params_in_pic_timing_sei_flag;
				if (parser.ReadBits(1, sub_pic_cpb_params_in_pic_timing_sei_flag) == false)
				{
					return false;
				}
				uint8_t dpb_output_delay_du_length_minus1;
				if (parser.ReadBits(5, dpb_output_delay_du_length_minus1) == false)
				{
					return false;
				}
			}

			uint8_t bit_rate_scale;
			if (parser.ReadBits(4, bit_rate_scale) == false)
			{
				return false;
			}
			uint8_t cpb_size_scale;
			if (parser.ReadBits(4, cpb_size_scale) == false)
			{
				return false;
			}

			if (sub_pic_hrd_params_present_flag == 1)
			{
				uint8_t cpb_size_du_scale;
				if (parser.ReadBits(4, cpb_size_du_scale) == false)
				{
					return false;
				}
			}

			uint8_t initial_cpb_removal_delay_length_minus1;
			if (parser.ReadBits(5, initial_cpb_removal_delay_length_minus1) == false)
			{
				return false;
			}
			uint8_t au_cpb_removal_delay_length_minus1;
			if (parser.ReadBits(5, au_cpb_removal_delay_length_minus1) == false)
			{
				return false;
			}
			uint8_t dpb_output_delay_length_minus1;
			if (parser.ReadBits(5, dpb_output_delay_length_minus1) == false)
			{
				return false;
			}
		}
	}

	for (uint32_t i = 0; i <= max_sub_layers_minus1; i++)
	{
		uint8_t fixed_pic_rate_general_flag;
		if (parser.ReadBits(1, fixed_pic_rate_general_flag) == false)
		{
			return false;
		}

		uint8_t fixed_pic_rate_within_cvs_flag = 0;
		if (fixed_pic_rate_general_flag == 1)
		{
			fixed_pic_rate_within_cvs_flag = 1;
		}
		else
		{
			if (parser.ReadBits(1, fixed_pic_rate_within_cvs_flag) == false)
			{
				return false;
			}
		}

		uint8_t low_delay_hrd_flag = 0;
		if (fixed_pic_rate_within_cvs_flag == 1)
		{
			uint32_t elemental_duration_in_tc_minus1;
			if (parser.ReadUEV(elemental_duration_in_tc_minus1) == false)
			{
				return false;
			}
		}
		else
		{
			if (parser.ReadBits(1, low_delay_hrd_flag) == false)
			{
				return false;
			}
		}

		uint32_t cpb_cnt_minus1 = 0;
		if (low_delay_hrd_flag == 1)
		{
			if (parser.ReadUEV(cpb_cnt_minus1) == false)
			{
				return false;
			}
		}

		if (nal_hrd_parameters_present_flag == 1)
		{
			SubLayerHrdParameters params;
			ProcessSubLayerHrdParameters(sub_pic_hrd_params_present_flag, cpb_cnt_minus1, parser, params);
		}

		if (vcl_hrd_parameters_present_flag == 1)
		{
			SubLayerHrdParameters params;
			ProcessSubLayerHrdParameters(sub_pic_hrd_params_present_flag, cpb_cnt_minus1, parser, params);
		}
	}

	return true;
}

bool H265Parser::ProcessSubLayerHrdParameters(uint8_t sub_pic_hrd_params_present_flag, uint32_t cpb_cnt, NalUnitBitstreamParser &parser, SubLayerHrdParameters &params)
{
	for (uint32_t i = 0; i <= cpb_cnt; i++)
	{
		uint32_t bit_rate_value_minus1;
		if (parser.ReadUEV(bit_rate_value_minus1) == false)
		{
			return false;
		}

		uint32_t cpb_size_value_minus1;
		if (parser.ReadUEV(cpb_size_value_minus1) == false)
		{
			return false;
		}

		if (sub_pic_hrd_params_present_flag == 1)
		{
			uint32_t cpb_size_du_value_minus1;
			if (parser.ReadUEV(cpb_size_du_value_minus1) == false)
			{
				return false;
			}

			uint32_t bit_rate_du_value_minus1;
			if (parser.ReadUEV(bit_rate_du_value_minus1) == false)
			{
				return false;
			}
		}

		uint8_t cbr_flag;
		if (parser.ReadBits(1, cbr_flag) == false)
		{
			return false;
		}
	}

	return true;
}

bool H265Parser::ProcessShortTermRefPicSet(uint32_t idx, uint32_t num_short_term_ref_pic_sets, const std::vector<ShortTermRefPicSet> &rpset_list, NalUnitBitstreamParser &parser, ShortTermRefPicSet &rpset)
{
	rpset.inter_ref_pic_set_prediction_flag = 0;
	rpset.delta_idx_minus1 = 0;

	if (idx)
	{
		if (parser.ReadBits(1, rpset.inter_ref_pic_set_prediction_flag) == false)
		{
			return false;
		}
	}

	if (rpset.inter_ref_pic_set_prediction_flag == 1)
	{
		if (idx == rpset.inter_ref_pic_set_prediction_flag)
		{
			if (parser.ReadUEV(rpset.delta_idx_minus1) == false)
			{
				return false;
			}
		}

		if (parser.ReadBits(1, rpset.delta_rps_sign) == false)
		{
			return false;
		}

		if (parser.ReadUEV(rpset.abs_delta_rps_minus1) == false)
		{
			return false;
		}

		uint32_t ref_rps_idx = idx - (rpset.delta_idx_minus1 + 1);
		uint32_t num_delta_pocs = 0;

		if (rpset_list[ref_rps_idx].inter_ref_pic_set_prediction_flag)
		{
			for (uint32_t i = 0; i < rpset_list[ref_rps_idx].used_by_curr_pic_flag.size(); i++)
			{
				if (rpset_list[ref_rps_idx].used_by_curr_pic_flag[i] || rpset_list[ref_rps_idx].use_delta_flag[i])
				{
					num_delta_pocs++;
				}
			}
		}
		else
		{
			num_delta_pocs = rpset_list[ref_rps_idx].num_negative_pics + rpset_list[ref_rps_idx].num_positive_pics;
		}

		rpset.used_by_curr_pic_flag.resize(num_delta_pocs + 1);
		rpset.use_delta_flag.resize(num_delta_pocs + 1, 1);

		for (uint32_t i = 0; i < num_delta_pocs; i++)
		{
			if (parser.ReadBits(1, rpset.used_by_curr_pic_flag[i]) == false)
			{
				return false;
			}
			if (!rpset.used_by_curr_pic_flag[i])
			{
				if (parser.ReadBits(1, rpset.use_delta_flag[i]) == false)
				{
					return false;
				}
			}
		}
	}
	else
	{
		if (parser.ReadUEV(rpset.num_negative_pics) == false || parser.ReadUEV(rpset.num_positive_pics) == false)
		{
			return false;
		}

		rpset.delta_poc_s0_minus1.resize(rpset.num_negative_pics);
		rpset.used_by_curr_pic_s0_flag.resize(rpset.num_negative_pics);

		for (std::size_t i = 0; i < rpset.num_negative_pics; i++)
		{
			if (parser.ReadUEV(rpset.delta_poc_s0_minus1[i]) == false)
			{
				return false;
			}
			if (parser.ReadBits(1, rpset.used_by_curr_pic_s0_flag[i]) == false)
			{
				return false;
			}
		}

		rpset.delta_poc_s1_minus1.resize(rpset.num_positive_pics);
		rpset.used_by_curr_pic_s1_flag.resize(rpset.num_positive_pics);
		for (std::size_t i = 0; i < rpset.num_positive_pics; i++)
		{
			if (parser.ReadUEV(rpset.delta_poc_s1_minus1[i]) == false)
			{
				return false;
			}
			if (parser.ReadBits(1, rpset.used_by_curr_pic_s1_flag[i]) == false)
			{
				return false;
			}
		}
	}
	return true;
}
