#include "h264_parser.h"

#include "h264_decoder_configuration_record.h"

#define OV_LOG_TAG "H264Parser"

std::vector<NaluIndex> H264Parser::FindNaluIndexes(const uint8_t *bitstream, size_t length)
{
	std::vector<NaluIndex> indexes;

	size_t offset = 0;
	while (offset < length)
	{
		size_t start_code_size = 0;
		auto pos = FindAnnexBStartCode(bitstream + offset, length - offset, start_code_size);
		offset += pos;

		// get previous index
		if (indexes.size() > 0)
		{
			auto& prev_index = indexes.back();

			// last NALU
			if (pos == -1)
			{
				prev_index._payload_size = length - prev_index._payload_offset;
				break;
			}

			prev_index._payload_size = offset - prev_index._payload_offset;
		}
		else if (pos == -1)
		{
			// error
			break;
		}
		
		// new NALU
		NaluIndex index;
		index._start_offset = offset;
		index._payload_offset = offset + start_code_size;
		index._payload_size = 0;

		indexes.push_back(index);

		offset += start_code_size;
	}

	return indexes;
}

int H264Parser::FindAnnexBStartCode(const uint8_t *bitstream, size_t length, size_t &start_code_size)
{
	size_t offset = 0;
	start_code_size = 0;

	while (offset < length)
	{
		size_t remaining = length - offset;
		const uint8_t *data = bitstream + offset;

		// fast forward to the next start code
		// if the 3rd byte isn't 1 or 0, we can skip 3 bytes
		if (remaining >= 3 && data[2] > 0x01)
		{
			offset += 3;
		}
		else if ((remaining >= 3 && data[0] == 0x00 && data[1] == 0x00 && data[2] == 0x01) ||
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

bool H264Parser::CheckAnnexBKeyframe(const uint8_t *bitstream, size_t length)
{
	size_t offset = 0;
	while (offset < length)
	{
		size_t start_code_size = 0;

		auto pos = FindAnnexBStartCode(bitstream + offset, length - offset, start_code_size);
		if (pos == -1)
		{
			break;
		}

		offset = offset + pos + start_code_size;
		if (length - offset > H264_NAL_UNIT_HEADER_SIZE)
		{
			H264NalUnitHeader header;
			ParseNalUnitHeader(bitstream + offset, H264_NAL_UNIT_HEADER_SIZE, header);

			if (header.GetNalUnitType() == H264NalUnitType::IdrSlice || header.GetNalUnitType() == H264NalUnitType::Sps)
			{
				return true;
			}
		}
	}

	return false;
}

bool H264Parser::ParseNalUnitHeader(const uint8_t *nalu, size_t length, H264NalUnitHeader &header)
{
	if (length < H264_NAL_UNIT_HEADER_SIZE)
	{
		logte("Invalid NALU header (length: %d)", length);
		return false;
	}

	uint8_t forbidden_zero_bit = (nalu[0] & 0x80) >> 7;
	if (forbidden_zero_bit != 0)
	{
		logte("Invalid NALU header (forbidden_zero_bit: %d)", forbidden_zero_bit);
		return false;
	}

	uint8_t nal_ref_idc = (nalu[0] & 0x60) >> 5;
	header._nal_ref_idc = nal_ref_idc;

	uint8_t nal_type = (nalu[0] & 0x1f);
	header._type = static_cast<H264NalUnitType>(nal_type);

	return true;
}

bool H264Parser::ParseNalUnitHeader(NalUnitBitstreamParser &parser, H264NalUnitHeader &header)
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

	uint8_t nal_ref_idc;
	if (parser.ReadBits(2, nal_ref_idc) == false)
	{
		return false;
	}
	header._nal_ref_idc = nal_ref_idc;

	uint8_t nal_type;
	if (parser.ReadBits(5, nal_type) == false)
	{
		return false;
	}

	header._type = static_cast<H264NalUnitType>(nal_type);

	return true;
}

bool H264Parser::ParseSPS(const uint8_t *nalu, size_t length, H264SPS &sps)
{
	// [ NAL ]
	NalUnitBitstreamParser parser(nalu, length);

	H264NalUnitHeader header;
	if (ParseNalUnitHeader(parser, header) == false)
	{
		return false;
	}

	if (header.GetNalUnitType() != H264NalUnitType::Sps)
	{
		return false;
	}

	// [ SPS ]

	if (!parser.ReadU8(sps._profile))
	{
		return false;
	}

	// Contraint set (5bits) and 3 reserved zero bits
	if (!parser.ReadU8(sps._constraint))
	{
		return false;
	}

	if (!parser.ReadU8(sps._codec_level))
	{
		return false;
	}

	if (!parser.ReadUEV(sps._id))
	{
		return false;
	}

	if (sps._profile == 44 || sps._profile == 83 || sps._profile == 86 || sps._profile == 100 ||
		sps._profile == 110 || sps._profile == 118 || sps._profile == 122 || sps._profile == 244 || sps._profile == 128)
	{
		if (!parser.ReadUEV(sps._chroma_format_idc))
		{
			return false;
		}

		if (sps._chroma_format_idc == 3)
		{
			// separate_colour_plane_flag
			if (!parser.Skip(1))
			{
				return false;
			}
		}

		uint32_t luma_bit_depth;
		if (!parser.ReadUEV(luma_bit_depth))
		{
			return false;
		}
		luma_bit_depth += 8;

		uint32_t chroma_bit_depth;
		if (!parser.ReadUEV(chroma_bit_depth))
		{
			return false;
		}
		chroma_bit_depth += 8;

		uint8_t zero_transform_bypass_flag;
		if (!parser.ReadBit(zero_transform_bypass_flag))
		{
			return false;
		}

		uint8_t scaling_matrix_present_flag;
		if (!parser.ReadBit(scaling_matrix_present_flag))
		{
			return false;
		}

		if (scaling_matrix_present_flag)
		{
			const size_t matrix_size = sps._chroma_format_idc == 3 ? 12 : 8;
			for (size_t index = 0; index < matrix_size; ++index)
			{
				uint8_t scaling_list_present_flag;
				if (!parser.ReadBit(scaling_list_present_flag))
				{
					return false;
				}

				if (scaling_list_present_flag)
				{
					// skip scaling list
					int32_t last_scale = 8, next_scale = 8;
					size_t size = index < 6 ? 16 : 64;

					for (size_t i = 0; i < size; i++)
					{
						if (next_scale != 0)
						{
							int32_t delta_scale;
							if (!parser.ReadSEV(delta_scale))
							{
								return false;
							}

							next_scale = (last_scale + delta_scale + 256) % 256;
						}

						if (next_scale != 0)
						{
							last_scale = next_scale;
						}
					}
				}
			}
		}
	}
	else
	{
		sps._chroma_format_idc = 1;
	}

	if (!parser.ReadUEV(sps._log2_max_frame_num_minus4))
	{
		return false;
	}

	if (!parser.ReadUEV(sps._pic_order_cnt_type))
	{
		return false;
	}

	if (sps._pic_order_cnt_type == 0)
	{
		if (!parser.ReadUEV(sps._log2_max_pic_order_cnt_lsb_minus4))
		{
			return false;
		}
	}
	else if (sps._pic_order_cnt_type == 1)
	{
		if (!parser.ReadBit(sps._delta_pic_order_always_zero_flag))
		{
			return false;
		}

		int32_t offset_for_non_ref_pic;
		if (!parser.ReadSEV(offset_for_non_ref_pic))
		{
			return false;
		}

		int32_t offset_for_top_to_bottom_field;
		if (!parser.ReadSEV(offset_for_top_to_bottom_field))
		{
			return false;
		}

		uint32_t num_ref_frames_in_pic_order_cnt_cycle;
		if (!parser.ReadUEV(num_ref_frames_in_pic_order_cnt_cycle))
		{
			return false;
		}

		for (uint32_t index = 0; index < num_ref_frames_in_pic_order_cnt_cycle; index++)
		{
			int32_t reference_frame_offset;
			if (!parser.ReadSEV(reference_frame_offset))
			{
				return false;
			}
		}
	}

	if (!parser.ReadUEV(sps._max_nr_of_reference_frames))
	{
		return false;
	}

	uint8_t gaps_in_frame_num_value_allowed_flag;
	if (!parser.ReadBit(gaps_in_frame_num_value_allowed_flag))
	{
		return false;
	}

	{
		// everything needed to determine the size lives in this block
		uint32_t pic_width_in_mbs_minus1;
		if (!parser.ReadUEV(pic_width_in_mbs_minus1))
		{
			return false;
		}

		uint32_t pic_height_in_map_units_minus1;
		if (!parser.ReadUEV(pic_height_in_map_units_minus1))
		{
			return false;
		}

		if (!parser.ReadBit(sps._frame_mbs_only_flag))
		{
			return false;
		}

		if (!sps._frame_mbs_only_flag)
		{
			uint8_t mb_adaptive_frame_field_flag;
			if (!parser.ReadBit(mb_adaptive_frame_field_flag))
			{
				return false;
			}
		}

		uint8_t direct_8x8_inference_flag;
		if (!parser.ReadBit(direct_8x8_inference_flag))
		{
			return false;
		}

		uint8_t frame_cropping_flag;
		if (!parser.ReadBit(frame_cropping_flag))
		{
			return false;
		}

		uint32_t crop_left = 0, crop_right = 0, crop_top = 0, crop_bottom = 0;
		if (frame_cropping_flag)
		{
			if (!parser.ReadUEV(crop_left))
			{
				return false;
			}

			if (!parser.ReadUEV(crop_right))
			{
				return false;
			}

			if (!parser.ReadUEV(crop_top))
			{
				return false;
			}

			if (!parser.ReadUEV(crop_bottom))
			{
				return false;
			}
		}

		// Calculate the display width and height
		// int64_t is used to avoid overflow for extreme cases
		int64_t coded_width	   = (pic_width_in_mbs_minus1 + 1) * 16;
		int64_t coded_height   = (2 - sps._frame_mbs_only_flag) * (pic_height_in_map_units_minus1 + 1) * 16;
		int64_t crop_x		   = 2 * crop_left + 2 * crop_right;
		int64_t crop_y		   = 2 * crop_top + 2 * crop_bottom;
		int64_t display_width  = coded_width - crop_x;
		int64_t display_height = coded_height - crop_y;

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
	}

	// [ VUI ]
	// VUI(Video Usability Information) is extended information of video. Since it is not a required part in ISO/IEC,
	// it is not an error even if the parsing fails.

	if (!ParseVUI(parser, sps))
	{
		logtw("Could not parsed VUI parameters of SPS");
	}

	return true;
}

bool H264Parser::ParseVUI(NalUnitBitstreamParser &parser, H264SPS &sps)
{
	uint8_t vui_parameters_present_flag;
	if (!parser.ReadBit(vui_parameters_present_flag))
	{
		return false;
	}

	if (vui_parameters_present_flag)
	{
		uint8_t aspect_ratio_info_present_flag;
		if (!parser.ReadBit(aspect_ratio_info_present_flag))
		{
			return false;
		}

		if (aspect_ratio_info_present_flag)
		{
			uint8_t aspect_ratio_idc;

			if (!parser.ReadU8(aspect_ratio_idc))
			{
				return false;
			}

			sps._aspect_ratio_idc = aspect_ratio_idc;

			if (aspect_ratio_idc == 255)
			{
				if (parser.ReadU16(sps._aspect_ratio._width) == false ||
					parser.ReadU16(sps._aspect_ratio._height) == false)
				{
					return false;
				}
			}
			else
			{
				// TODO: set the aspect ratio from the table mapping aspect_ratio_idc to actual values from the
				// H.264 spec
			}
		}

		{
			uint8_t overscan_info_present_flag;
			if (!parser.ReadBit(overscan_info_present_flag))
			{
				return false;
			}
			if (overscan_info_present_flag)
			{
				if (uint8_t overscan_appropriate_flag; !parser.ReadBit(overscan_appropriate_flag))
				{
					return false;
				}
			}
		}

		{
			uint8_t video_signal_type_present_flag;
			if (!parser.ReadBit(video_signal_type_present_flag))
			{
				return false;
			}

			if (video_signal_type_present_flag)
			{
				{
					uint8_t video_format = 0, bit;
					if (!(parser.ReadBit(bit) &&
						  (video_format |= bit, video_format <<= 1, parser.ReadBit(bit)) &&
						  (video_format |= bit, video_format <<= 1, parser.ReadBit(bit))))
					{
						return false;
					}
					video_format |= bit;
				}

				if (uint8_t video_full_range_flag; !parser.ReadBit(video_full_range_flag))
				{
					return false;
				}

				uint8_t colour_description_present_flag;
				if (!parser.ReadBit(colour_description_present_flag))
				{
					return false;
				}

				if (colour_description_present_flag)
				{
					if (uint8_t colour_primaries; !parser.ReadU8(colour_primaries))
					{
						return false;
					}

					if (uint8_t transfer_characteristics; !parser.ReadU8(transfer_characteristics))
					{
						return false;
					}

					if (uint8_t matrix_coefficients; !parser.ReadU8(matrix_coefficients))
					{
						return false;
					}
				}
			}
		}

		{
			uint8_t chroma_loc_info_present_flag;
			if (!parser.ReadBit(chroma_loc_info_present_flag))
			{
				return false;
			}

			if (chroma_loc_info_present_flag)
			{
				uint32_t chroma_sample_loc_type_top_field;
				if (!parser.ReadUEV(chroma_sample_loc_type_top_field))
				{
					return false;
				}
				uint32_t chroma_sample_loc_type_bottom_field;
				if (!parser.ReadUEV(chroma_sample_loc_type_bottom_field))
				{
					return false;
				}
			}
		}

		{
			uint8_t timing_info_present_flag;
			if (!parser.ReadBit(timing_info_present_flag))
			{
				return false;
			}

			if (timing_info_present_flag)
			{
				uint32_t num_units_in_tick, time_scale;
				uint8_t fixed_frame_rate_flag;

				if (!parser.ReadUEV(num_units_in_tick))
				{
					return false;
				}

				if (!parser.ReadUEV(time_scale))
				{
					return false;
				}

				if (!parser.ReadBit(fixed_frame_rate_flag))
				{
					return false;
				}

				if (fixed_frame_rate_flag)
				{
					sps._fps = (num_units_in_tick != 0)
								   ? (time_scale / (2 * num_units_in_tick))
								   : 0;
				}
			}
		}
		// Currently skip the remaining part of VUI parameters
	}

	return true;
}

bool H264Parser::ParsePPS(const uint8_t *nalu, size_t length, H264PPS &pps)
{
	NalUnitBitstreamParser parser(nalu, length);

	H264NalUnitHeader nal_header;
	if (ParseNalUnitHeader(parser, nal_header) == false)
	{
		return false;
	}

	if (nal_header.GetNalUnitType() != H264NalUnitType::Pps)
	{
		return false;
	}

	// pps id
	if (!parser.ReadUEV(pps._pps_id))
	{
		return false;
	}

	// sps id
	if (!parser.ReadUEV(pps._sps_id))
	{
		return false;
	}

	// entropy_coding_mode_flag

	if (!parser.ReadBit(pps._entropy_coding_mode_flag))
	{
		return false;
	}

	if (!parser.ReadBit(pps._bottom_field_pic_order_in_frame_present_flag))
	{
		return false;
	}

	// num_slice_groups_minus1

	if (!parser.ReadUEV(pps._num_slice_groups_minus1))
	{
		return false;
	}

	if (pps._num_slice_groups_minus1 > 1)
	{
		// not support
		return false;
	}

	if (!parser.ReadUEV(pps._num_ref_idx_l0_default_active_minus1))
	{
		return false;
	}

	if (!parser.ReadUEV(pps._num_ref_idx_l1_default_active_minus1))
	{
		return false;
	}

	if (!parser.ReadBit(pps._weighted_pred_flag))
	{
		return false;
	}

	if (!parser.ReadBits(2, pps._weighted_bipred_idc))
	{
		return false;
	}

	int32_t pic_init_qp_minus26;
	if (!parser.ReadSEV(pic_init_qp_minus26))
	{
		return false;
	}

	int32_t pic_init_qs_minus26;
	if (!parser.ReadSEV(pic_init_qs_minus26))
	{
		return false;
	}

	int32_t chroma_qp_index_offset;
	if (!parser.ReadSEV(chroma_qp_index_offset))
	{
		return false;
	}

	if (!parser.ReadBit(pps._deblocking_filter_control_present_flag))
	{
		return false;
	}

	uint8_t constrained_intra_pred_flag;
	if (!parser.ReadBit(constrained_intra_pred_flag))
	{
		return false;
	}

	if (!parser.ReadBit(pps._redundant_pic_cnt_present_flag))
	{
		return false;
	}

	// skip the rest of the PPS, we don't need it

	return true;
}

bool H264Parser::ParseSliceHeader(const uint8_t *nalu, size_t length, H264SliceHeader &shd, std::shared_ptr<AVCDecoderConfigurationRecord> &avcc)
{
	NalUnitBitstreamParser parser(nalu, length);

	H264NalUnitHeader nal_header;
	if (ParseNalUnitHeader(parser, nal_header) == false)
	{
		return false;
	}

	if (nal_header.IsVideoSlice() == false)
	{
		return false;
	}

	uint32_t first_mb_in_slice;
	if (!parser.ReadUEV(first_mb_in_slice))
	{
		return false;
	}

	if (!parser.ReadUEV(shd._slice_type))
	{
		return false;
	}
	if (shd._slice_type > 9)
	{
		return false;
	}

	uint32_t pic_parameter_set_id;
	if (!parser.ReadUEV(pic_parameter_set_id))
	{
		return false;
	}

	H264PPS pps;
	if (avcc->GetPPS(pic_parameter_set_id, pps) == false)
	{
		return false;
	}

	H264SPS sps;
	if (avcc->GetSPS(pps._sps_id, sps) == false)
	{
		return false;
	}

	// TODO ?? interlaced source is not supported
	// separate_colour_plane_flag ??
	// if (sps._separate_colour_plane_flag)
	// {
	// 	uint8_t colour_plane_id;
	// 	if (!parser.ReadBits(2, colour_plane_id))
	// 	{
	// 		return false;
	// 	}
	// }

	uint32_t frame_num = 0;
	if (!parser.ReadBits(sps._log2_max_frame_num_minus4 + 4, frame_num))
	{
		return false;
	}

	bool field_pic_flag = false;
	if (sps._frame_mbs_only_flag == 0)
	{
		if (!parser.ReadBit(field_pic_flag))
		{
			return false;
		}

		if (field_pic_flag)
		{
			uint8_t bottom_field_flag;
			if (!parser.ReadBit(bottom_field_flag))
			{
				return false;
			}
		}
	}

	if (nal_header.GetNalUnitType() == H264NalUnitType::IdrSlice)
	{
		uint32_t idr_pic_id;
		if (!parser.ReadUEV(idr_pic_id))
		{
			return false;
		}
	}

	if (sps._pic_order_cnt_type == 0)
	{
		uint32_t pic_order_cnt_lsb;
		if (!parser.ReadBits(sps._log2_max_pic_order_cnt_lsb_minus4 + 4, pic_order_cnt_lsb))
		{
			return false;
		}

		if (pps._bottom_field_pic_order_in_frame_present_flag && !field_pic_flag)
		{
			int32_t delta_pic_order_cnt_bottom;
			if (!parser.ReadSEV(delta_pic_order_cnt_bottom))
			{
				return false;
			}
		}
	}

	if (sps._pic_order_cnt_type == 1 && !sps._delta_pic_order_always_zero_flag)
	{
		int32_t delta_pic_order_cnt[2];
		if (!parser.ReadSEV(delta_pic_order_cnt[0]))
		{
			return false;
		}

		if (pps._bottom_field_pic_order_in_frame_present_flag && !field_pic_flag)
		{
			if (!parser.ReadSEV(delta_pic_order_cnt[1]))
			{
				return false;
			}
		}
	}

	if (pps._redundant_pic_cnt_present_flag)
	{
		uint32_t redundant_pic_cnt;
		if (!parser.ReadUEV(redundant_pic_cnt))
		{
			return false;
		}
	}

	if (shd.GetSliceType() == H264SliceHeader::SliceType::BSlice)
	{
		uint8_t direct_spatial_mv_pred_flag;
		if (!parser.ReadBit(direct_spatial_mv_pred_flag))
		{
			return false;
		}
	}

	if (shd.GetSliceType() == H264SliceHeader::SliceType::PSlice ||
		shd.GetSliceType() == H264SliceHeader::SliceType::BSlice ||
		shd.GetSliceType() == H264SliceHeader::SliceType::SPSlice)
	{
		bool num_ref_idx_active_override_flag;
		if (!parser.ReadBit(num_ref_idx_active_override_flag))
		{
			return false;
		}

		if (num_ref_idx_active_override_flag)
		{
			if (!parser.ReadUEV(shd._num_ref_idx_l0_active_minus1))
			{
				return false;
			}

			if (shd.GetSliceType() == H264SliceHeader::SliceType::BSlice)
			{
				if (!parser.ReadUEV(shd._num_ref_idx_l1_active_minus1))
				{
					return false;
				}
			}
		}
		else
		{
			shd._num_ref_idx_l0_active_minus1 = pps._num_ref_idx_l0_default_active_minus1;

			if (shd.GetSliceType() == H264SliceHeader::SliceType::BSlice)
			{
				shd._num_ref_idx_l1_active_minus1 = pps._num_ref_idx_l1_default_active_minus1;
			}
		}
	}

	if (!ParseRefPicListReordering(parser, shd))
	{
		return false;
	}

	if ((pps._weighted_pred_flag && (shd.GetSliceType() == H264SliceHeader::SliceType::PSlice || shd.GetSliceType() == H264SliceHeader::SliceType::SPSlice)) ||

		(pps._weighted_bipred_idc == 1 && shd.GetSliceType() == H264SliceHeader::SliceType::BSlice))
	{
		if (!ParsePredWeightTable(parser, sps, shd))
		{
			return false;
		}
	}

	if (nal_header._nal_ref_idc != 0)
	{
		ParseDecRefPicMarking(parser, nal_header.GetNalUnitType() == H264NalUnitType::IdrSlice, shd);
	}

	if (pps._entropy_coding_mode_flag && shd.GetSliceType() != H264SliceHeader::SliceType::ISlice && shd.GetSliceType() != H264SliceHeader::SliceType::SISlice)
	{
		uint32_t cabac_init_idc;
		if (!parser.ReadUEV(cabac_init_idc))
		{
			return false;
		}
	}

	int32_t slice_qp_delta;
	if (!parser.ReadSEV(slice_qp_delta))
	{
		return false;
	}

	if (shd.GetSliceType() == H264SliceHeader::SliceType::SPSlice || shd.GetSliceType() == H264SliceHeader::SliceType::SISlice)
	{
		if (shd.GetSliceType() == H264SliceHeader::SliceType::SPSlice)
		{
			uint8_t sp_for_switch_flag;
			if (!parser.ReadBit(sp_for_switch_flag))
			{
				return false;
			}
		}

		int32_t slice_qs_delta;
		if (!parser.ReadSEV(slice_qs_delta))
		{
			return false;
		}
	}

	if (pps._deblocking_filter_control_present_flag)
	{
		uint32_t disable_deblocking_filter_idc;
		if (!parser.ReadUEV(disable_deblocking_filter_idc))
		{
			return false;
		}

		if (disable_deblocking_filter_idc != 1)
		{
			int32_t slice_alpha_c0_offset_div2;
			if (!parser.ReadSEV(slice_alpha_c0_offset_div2))
			{
				return false;
			}

			int32_t slice_beta_offset_div2;
			if (!parser.ReadSEV(slice_beta_offset_div2))
			{
				return false;
			}
		}
	}

	if (pps._num_slice_groups_minus1 > 0)
	{
		// not support
		return false;
	}

	shd._header_size_in_bits = parser.BitsConsumed() - (H264_NAL_UNIT_HEADER_SIZE * 8);

	return true;
}

bool H264Parser::ParseRefPicListReordering(NalUnitBitstreamParser &parser, H264SliceHeader &header)
{
	if (header.GetSliceType() != H264SliceHeader::SliceType::ISlice &&
		header.GetSliceType() != H264SliceHeader::SliceType::SISlice)
	{
		uint8_t ref_pic_list_reordering_flag_l0;
		if (!parser.ReadBit(ref_pic_list_reordering_flag_l0))
		{
			return false;
		}

		if (ref_pic_list_reordering_flag_l0)
		{
			uint32_t reordering_of_pic_nums_idc;
			do
			{
				if (!parser.ReadUEV(reordering_of_pic_nums_idc))
				{
					return false;
				}

				if (reordering_of_pic_nums_idc == 0 || reordering_of_pic_nums_idc == 1)
				{
					uint32_t abs_diff_pic_num_minus1;
					if (!parser.ReadUEV(abs_diff_pic_num_minus1))
					{
						return false;
					}
				}
				else if (reordering_of_pic_nums_idc == 2)
				{
					uint32_t long_term_pic_num;
					if (!parser.ReadUEV(long_term_pic_num))
					{
						return false;
					}
				}
			} while (reordering_of_pic_nums_idc != 3);
		}
	}

	if (header.GetSliceType() == H264SliceHeader::SliceType::BSlice)
	{
		uint8_t ref_pic_list_reordering_flag_l1;
		if (!parser.ReadBit(ref_pic_list_reordering_flag_l1))
		{
			return false;
		}

		if (ref_pic_list_reordering_flag_l1)
		{
			uint32_t reordering_of_pic_nums_idc;
			do
			{
				if (!parser.ReadUEV(reordering_of_pic_nums_idc))
				{
					return false;
				}

				if (reordering_of_pic_nums_idc == 0 || reordering_of_pic_nums_idc == 1)
				{
					uint32_t abs_diff_pic_num_minus1;
					if (!parser.ReadUEV(abs_diff_pic_num_minus1))
					{
						return false;
					}
				}
				else if (reordering_of_pic_nums_idc == 2)
				{
					uint32_t long_term_pic_num;
					if (!parser.ReadUEV(long_term_pic_num))
					{
						return false;
					}
				}
			} while (reordering_of_pic_nums_idc != 3);
		}
	}

	return true;
}

bool H264Parser::ParsePredWeightTable(NalUnitBitstreamParser &parser, const H264SPS &sps, H264SliceHeader &header)
{
	uint32_t luma_log2_weight_denom;
	if (!parser.ReadUEV(luma_log2_weight_denom))
	{
		return false;
	}

	if (sps._chroma_format_idc != 0)
	{
		uint32_t chroma_log2_weight_denom;
		parser.ReadUEV(chroma_log2_weight_denom);
	}

	for (uint32_t index = 0; index <= header._num_ref_idx_l0_active_minus1; index++)
	{
		uint8_t luma_weight_l0_flag;
		parser.ReadBit(luma_weight_l0_flag);

		if (luma_weight_l0_flag)
		{
			int32_t luma_weight_l0;
			parser.ReadSEV(luma_weight_l0);

			int32_t luma_offset_l0;
			parser.ReadSEV(luma_offset_l0);
		}

		if (sps._chroma_format_idc != 0)
		{
			uint8_t chroma_weight_l0_flag;
			parser.ReadBit(chroma_weight_l0_flag);

			if (chroma_weight_l0_flag)
			{
				for (uint32_t index = 0; index < 2; index++)
				{
					int32_t chroma_weight_l0;
					parser.ReadSEV(chroma_weight_l0);

					int32_t chroma_offset_l0;
					parser.ReadSEV(chroma_offset_l0);
				}
			}
		}
	}

	if (header.GetSliceType() == H264SliceHeader::SliceType::BSlice)
	{
		for (uint32_t index = 0; index <= header._num_ref_idx_l1_active_minus1; index++)
		{
			uint8_t luma_weight_l1_flag;
			parser.ReadBit(luma_weight_l1_flag);

			if (luma_weight_l1_flag)
			{
				int32_t luma_weight_l1;
				parser.ReadSEV(luma_weight_l1);

				int32_t luma_offset_l1;
				parser.ReadSEV(luma_offset_l1);
			}

			if (sps._chroma_format_idc != 0)
			{
				uint8_t chroma_weight_l1_flag;
				parser.ReadBit(chroma_weight_l1_flag);

				if (chroma_weight_l1_flag)
				{
					for (uint32_t index = 0; index < 2; index++)
					{
						int32_t chroma_weight_l1;
						parser.ReadSEV(chroma_weight_l1);

						int32_t chroma_offset_l1;
						parser.ReadSEV(chroma_offset_l1);
					}
				}
			}
		}
	}

	return true;
}

bool H264Parser::ParseDecRefPicMarking(NalUnitBitstreamParser &parser, bool idr, H264SliceHeader &header)
{
	if (idr)
	{
		uint8_t no_output_of_prior_pics_flag;
		if (!parser.ReadBit(no_output_of_prior_pics_flag))
		{
			return false;
		}

		uint8_t long_term_reference_flag;
		if (!parser.ReadBit(long_term_reference_flag))
		{
			return false;
		}
	}
	else
	{
		uint8_t adaptive_ref_pic_marking_mode_flag;
		if (!parser.ReadBit(adaptive_ref_pic_marking_mode_flag))
		{
			return false;
		}

		if (adaptive_ref_pic_marking_mode_flag)
		{
			uint32_t memory_management_control_operation;
			do
			{
				if (!parser.ReadUEV(memory_management_control_operation))
				{
					return false;
				}

				if (memory_management_control_operation == 1 || memory_management_control_operation == 3)
				{
					uint32_t difference_of_pic_nums_minus1;
					if (!parser.ReadUEV(difference_of_pic_nums_minus1))
					{
						return false;
					}
				}

				if (memory_management_control_operation == 2)
				{
					uint32_t long_term_pic_num;
					if (!parser.ReadUEV(long_term_pic_num))
					{
						return false;
					}
				}

				if (memory_management_control_operation == 3 || memory_management_control_operation == 6)
				{
					uint32_t long_term_frame_idx;
					if (!parser.ReadUEV(long_term_frame_idx))
					{
						return false;
					}
				}

				if (memory_management_control_operation == 4)
				{
					uint32_t max_long_term_frame_idx_plus1;
					if (!parser.ReadUEV(max_long_term_frame_idx_plus1))
					{
						return false;
					}
				}
			} while (memory_management_control_operation != 0);
		}
	}

	return true;
}