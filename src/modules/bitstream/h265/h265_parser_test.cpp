//==============================================================================
//
//  OvenMediaEngine - Unit Tests
//
//  Covers H265Parser (SPS/PPS parsing, slice-segment-header size accounting)
//  and HEVCDecoderConfigurationRecord SPS/PPS lookup (id range validation).
//
//  Bitstreams are hand-built with a BitWriter so the expected values
//  (GetHeaderSizeInBytes(), slice type, resolution, ...) are known by
//  construction rather than captured from the parser under test.
//  Syntax follows Rec. ITU-T H.265 (7.3.2.2 SPS, 7.3.2.3 PPS, 7.3.6.1 slice).
//
//==============================================================================

// Unit tests
// ----------
// cmake build/debug && ninja -C build/debug ome_test_modules
// ./build/debug/bin/ome_test_modules --gtest_filter='H265Parser.*:H265DecoderConfig.*'

#include <gtest/gtest.h>

#include <base/ovlibrary/bit_writer.h>
#include <base/ovlibrary/data.h>
#include <modules/bitstream/h265/h265_decoder_configuration_record.h>
#include <modules/bitstream/h265/h265_parser.h>

#include <memory>
#include <vector>

namespace
{
	// ---- Exp-Golomb writers (Rec. ITU-T H.265 9.2) ----
	void WriteUE(ov::BitWriter &w, uint32_t v)
	{
		uint64_t n = static_cast<uint64_t>(v) + 1;
		int numbits = 0;
		while ((n >> (numbits + 1)) != 0)
		{
			numbits++;
		}
		if (numbits > 0)
		{
			w.WriteBits(numbits, 0);
		}
		w.WriteBits(numbits + 1, n);
	}

	void WriteSE(ov::BitWriter &w, int32_t v)
	{
		uint32_t code = (v <= 0) ? static_cast<uint32_t>(-2 * static_cast<int64_t>(v))
								 : static_cast<uint32_t>(2 * static_cast<int64_t>(v) - 1);
		WriteUE(w, code);
	}

	// rbsp_trailing_bits(): stop bit '1' then zero-pad to a byte boundary.
	void WriteTrailing(ov::BitWriter &w)
	{
		w.WriteBits(1, 1);
		while (w.GetBitCount() % 8 != 0)
		{
			w.WriteBits(1, 0);
		}
	}

	std::vector<uint8_t> ToBytes(ov::BitWriter &w)
	{
		return std::vector<uint8_t>(w.GetData(), w.GetData() + w.GetDataSize());
	}

	// Insert emulation_prevention_three_byte into an RBSP payload to form an EBSP.
	std::vector<uint8_t> ApplyEmulationPrevention(const std::vector<uint8_t> &rbsp)
	{
		std::vector<uint8_t> out;
		out.reserve(rbsp.size() + 4);
		size_t zeros = 0;
		for (uint8_t b : rbsp)
		{
			if (zeros >= 2 && b <= 0x03)
			{
				out.push_back(0x03);
				zeros = 0;
			}
			out.push_back(b);
			zeros = (b == 0x00) ? zeros + 1 : 0;
		}
		return out;
	}

	// Build a NAL unit: 2-byte header (nal_type) + emulation-prevented RBSP.
	std::vector<uint8_t> MakeNal(uint8_t nal_type, const std::vector<uint8_t> &rbsp)
	{
		std::vector<uint8_t> nal;
		nal.push_back(static_cast<uint8_t>((nal_type << 1) & 0x7E));	 // forbidden=0, layerId hi=0
		nal.push_back(0x01);										 // layerId lo=0, tid_plus1=1
		auto ebsp = ApplyEmulationPrevention(rbsp);
		nal.insert(nal.end(), ebsp.begin(), ebsp.end());
		return nal;
	}

	std::shared_ptr<ov::Data> ToData(const std::vector<uint8_t> &bytes)
	{
		return std::make_shared<ov::Data>(bytes.data(), bytes.size());
	}

	// Ceil(Log2(n)) : bits needed to represent [0, n-1].
	// Mirrors the parser's helper, including its bits < 32 guard.
	uint32_t CeilLog2(uint32_t n)
	{
		uint32_t bits = 0;
		while (bits < 32 && (1u << bits) < n)
		{
			bits++;
		}
		return bits;
	}

	// Out-of-range SPS values for the range guards; defaults are conformant.
	struct SpsOverrides
	{
		uint32_t pic_width = 320;
		uint32_t pic_height = 240;
		uint32_t num_negative_pics = 1;
		uint32_t num_positive_pics = 0;
		bool long_term_ref_pics_present = false;
		uint32_t num_long_term_ref_pics_sps = 0;
	};

	// ---- Minimal VPS (Rec. ITU-T H.265 7.3.2.1). Only needed so a record becomes valid. ----
	std::vector<uint8_t> BuildVps()
	{
		ov::BitWriter w(32);
		w.WriteBits(4, 0);		// vps_video_parameter_set_id
		w.WriteBits(1, 1);		// vps_base_layer_internal_flag
		w.WriteBits(1, 1);		// vps_base_layer_available_flag
		w.WriteBits(6, 0);		// vps_max_layers_minus1
		w.WriteBits(3, 0);		// vps_max_sub_layers_minus1
		w.WriteBits(1, 0);		// vps_temporal_id_nesting_flag
		w.WriteBits(16, 0xFFFF);  // vps_reserved_0xffff_16bits

		// profile_tier_level (max_sub_layers_minus1 == 0 -> 96 bits)
		w.WriteBits(2, 0);			  // general_profile_space
		w.WriteBits(1, 0);			  // general_tier_flag
		w.WriteBits(5, 1);			  // general_profile_idc (Main)
		w.WriteBits(32, 0x60000000);  // general_profile_compatibility_flags
		w.WriteBits(32, 0);			  // general_constraint_indicator_flags (hi 32)
		w.WriteBits(16, 0);			  // general_constraint_indicator_flags (lo 16)
		w.WriteBits(8, 93);			  // general_level_idc (3.1)

		w.WriteBits(1, 0);	// vps_sub_layer_ordering_info_present_flag -> one iteration
		WriteUE(w, 0);	// vps_max_dec_pic_buffering_minus1
		WriteUE(w, 0);	// vps_max_num_reorder_pics
		WriteUE(w, 0);	// vps_max_latency_increase_plus1

		w.WriteBits(6, 0);	// vps_max_layer_id
		WriteUE(w, 0);	// vps_num_layer_sets_minus1
		w.WriteBits(1, 0);	// vps_timing_info_present_flag
		w.WriteBits(1, 0);	// vps_extension_flag

		WriteTrailing(w);
		return MakeNal(32, ToBytes(w));	 // VPS
	}

	// ---- Minimal SPS (4:2:0, 320x240, SAO configurable) ----
	// num_strps short-term RPS are stored, each with num_negative_pics=1
	// (delta_poc_s0_minus1=0, used_by_curr_pic_s0_flag=1), num_positive_pics=0
	// -> NumDeltaPocs == 1. log2_diff selects CtbLog2SizeY (= 3 + log2_diff).
	std::vector<uint8_t> BuildSps(bool sao_enabled, uint32_t num_strps = 0, uint32_t log2_diff = 3,
								  const SpsOverrides &overrides = {},
								  uint32_t log2_max_pic_order_cnt_lsb_minus4 = 0,
								  uint32_t sps_id = 0)
	{
		ov::BitWriter w(64);
		w.WriteBits(4, 0);	// sps_video_parameter_set_id
		w.WriteBits(3, 0);	// sps_max_sub_layers_minus1
		w.WriteBits(1, 0);	// sps_temporal_id_nesting_flag

		// profile_tier_level (max_sub_layers_minus1 == 0 -> 96 bits)
		w.WriteBits(2, 0);			 // general_profile_space
		w.WriteBits(1, 0);			 // general_tier_flag
		w.WriteBits(5, 1);			 // general_profile_idc (Main)
		w.WriteBits(32, 0x60000000); // general_profile_compatibility_flags
		w.WriteBits(32, 0);			 // general_constraint_indicator_flags (hi 32)
		w.WriteBits(16, 0);			 // general_constraint_indicator_flags (lo 16)
		w.WriteBits(8, 93);			 // general_level_idc (3.1)

		WriteUE(w, sps_id);	// sps_seq_parameter_set_id
		WriteUE(w, 1);	// chroma_format_idc (4:2:0)
		WriteUE(w, overrides.pic_width);   // pic_width_in_luma_samples
		WriteUE(w, overrides.pic_height);  // pic_height_in_luma_samples
		w.WriteBits(1, 0);	// conformance_window_flag
		WriteUE(w, 0);	// bit_depth_luma_minus8
		WriteUE(w, 0);	// bit_depth_chroma_minus8
		WriteUE(w, log2_max_pic_order_cnt_lsb_minus4);	// log2_max_pic_order_cnt_lsb_minus4

		w.WriteBits(1, 0);	// sps_sub_layer_ordering_info_present_flag -> one iteration
		WriteUE(w, 0);	// sps_max_dec_pic_buffering_minus1
		WriteUE(w, 0);	// sps_max_num_reorder_pics
		WriteUE(w, 0);	// sps_max_latency_increase_plus1

		WriteUE(w, 0);	// log2_min_luma_coding_block_size_minus3 (MinCbLog2SizeY=3)
		WriteUE(w, log2_diff);	// log2_diff_max_min_luma_coding_block_size (CtbLog2SizeY = 3 + log2_diff)
		WriteUE(w, 0);	// log2_min_transform_block_size_minus2
		WriteUE(w, 3);	// log2_diff_max_min_transform_block_size
		WriteUE(w, 0);	// max_transform_hierarchy_depth_inter
		WriteUE(w, 0);	// max_transform_hierarchy_depth_intra
		w.WriteBits(1, 0);	// scaling_list_enabled_flag
		w.WriteBits(1, 0);	// amp_enabled_flag
		w.WriteBits(1, sao_enabled ? 1 : 0);  // sample_adaptive_offset_enabled_flag
		w.WriteBits(1, 0);	// pcm_enabled_flag
		WriteUE(w, num_strps);	// num_short_term_ref_pic_sets
		for (uint32_t i = 0; i < num_strps; i++)
		{
			if (i > 0)
			{
				w.WriteBits(1, 0);	// inter_ref_pic_set_prediction_flag (idx > 0)
			}
			WriteUE(w, overrides.num_negative_pics);	// num_negative_pics
			WriteUE(w, overrides.num_positive_pics);	// num_positive_pics
			for (uint32_t j = 0; j < overrides.num_negative_pics; j++)
			{
				WriteUE(w, 0);	// delta_poc_s0_minus1[j]
				w.WriteBits(1, 1);	// used_by_curr_pic_s0_flag[j]
			}
			for (uint32_t j = 0; j < overrides.num_positive_pics; j++)
			{
				WriteUE(w, 0);	// delta_poc_s1_minus1[j]
				w.WriteBits(1, 1);	// used_by_curr_pic_s1_flag[j]
			}
		}

		w.WriteBits(1, overrides.long_term_ref_pics_present ? 1 : 0);  // long_term_ref_pics_present_flag
		if (overrides.long_term_ref_pics_present)
		{
			WriteUE(w, overrides.num_long_term_ref_pics_sps);
			for (uint32_t i = 0; i < overrides.num_long_term_ref_pics_sps; i++)
			{
				// lt_ref_pic_poc_lsb_sps[i] is u(log2_max_pic_order_cnt_lsb_minus4 + 4).
				w.WriteBits(log2_max_pic_order_cnt_lsb_minus4 + 4, 0);
				w.WriteBits(1, 0);	// used_by_curr_pic_lt_sps_flag[i]
			}
		}
		w.WriteBits(1, 0);	// sps_temporal_mvp_enabled_flag
		w.WriteBits(1, 0);	// strong_intra_smoothing_enabled_flag
		w.WriteBits(1, 0);	// vui_parameters_present_flag
		w.WriteBits(1, 0);	// sps_extension_flag

		WriteTrailing(w);
		return MakeNal(33, ToBytes(w));
	}

	// ---- Minimal PPS (all optional/flagged syntax disabled) ----
	// range_extension sets pps_extension_present_flag + pps_range_extension_flag so the
	// slice-header parser must fail-safe (transform_skip_enabled_flag is 0, so the range
	// extension carries no extra payload).
	// entropy_coding_sync selects WPP: the slice header gains num_entry_point_offsets
	// and, unlike tiles_enabled_flag, the PPS gains no syntax of its own.
	std::vector<uint8_t> BuildPps(uint32_t num_extra_slice_header_bits = 0, bool range_extension = false,
								  uint32_t num_ref_idx_default_active_minus1 = 0,
								  bool entropy_coding_sync = false,
								  uint32_t pps_id = 0, uint32_t sps_id = 0)
	{
		ov::BitWriter w(32);
		WriteUE(w, pps_id);	// pps_pic_parameter_set_id
		WriteUE(w, sps_id);	// pps_seq_parameter_set_id
		w.WriteBits(1, 0);	// dependent_slice_segments_enabled_flag
		w.WriteBits(1, 0);	// output_flag_present_flag
		w.WriteBits(3, num_extra_slice_header_bits);  // num_extra_slice_header_bits
		w.WriteBits(1, 0);	// sign_data_hiding_enabled_flag
		w.WriteBits(1, 0);	// cabac_init_present_flag
		WriteUE(w, num_ref_idx_default_active_minus1);	// num_ref_idx_l0_default_active_minus1
		WriteUE(w, num_ref_idx_default_active_minus1);	// num_ref_idx_l1_default_active_minus1
		WriteSE(w, 0);	// init_qp_minus26
		w.WriteBits(1, 0);	// constrained_intra_pred_flag
		w.WriteBits(1, 0);	// transform_skip_enabled_flag
		w.WriteBits(1, 0);	// cu_qp_delta_enabled_flag
		WriteSE(w, 0);	// pps_cb_qp_offset
		WriteSE(w, 0);	// pps_cr_qp_offset
		w.WriteBits(1, 0);	// pps_slice_chroma_qp_offsets_present_flag
		w.WriteBits(1, 0);	// weighted_pred_flag
		w.WriteBits(1, 0);	// weighted_bipred_flag
		w.WriteBits(1, 0);	// transquant_bypass_enabled_flag
		w.WriteBits(1, 0);	// tiles_enabled_flag
		w.WriteBits(1, entropy_coding_sync ? 1 : 0);  // entropy_coding_sync_enabled_flag
		w.WriteBits(1, 0);	// pps_loop_filter_across_slices_enabled_flag
		w.WriteBits(1, 0);	// deblocking_filter_control_present_flag
		w.WriteBits(1, 0);	// pps_scaling_list_data_present_flag
		w.WriteBits(1, 0);	// lists_modification_present_flag
		WriteUE(w, 0);	// log2_parallel_merge_level_minus2
		w.WriteBits(1, 0);	// slice_segment_header_extension_present_flag
		w.WriteBits(1, range_extension ? 1 : 0);  // pps_extension_present_flag
		if (range_extension)
		{
			w.WriteBits(1, 1);	// pps_range_extension_flag
			w.WriteBits(1, 0);	// pps_multilayer_extension_flag
			w.WriteBits(1, 0);	// pps_3d_extension_flag
			w.WriteBits(1, 0);	// pps_scc_extension_flag
			w.WriteBits(4, 0);	// pps_extension_4bits
			// pps_range_extension() is empty because transform_skip_enabled_flag == 0.
		}

		WriteTrailing(w);
		return MakeNal(34, ToBytes(w));
	}

	// ---- IDR (I) slice segment header. SAO flags present iff sao_enabled. ----
	// Header syntax bits (before byte_alignment):
	//   first_slice(1) + no_output(1) + pps_id ue(0)=1 + slice_type ue(2)=3
	//   [+ sao_luma(1) + sao_chroma(1) when sao] + slice_qp_delta se(0)=1
	//   = 7 (sao off) or 9 (sao on) bits; byte_alignment rounds up to 1 / 2 bytes.
	std::vector<uint8_t> BuildIdrSlice(bool sao_enabled, uint32_t slice_type = 2)
	{
		ov::BitWriter w(16);
		w.WriteBits(1, 1);	// first_slice_segment_in_pic_flag
		w.WriteBits(1, 0);	// no_output_of_prior_pics_flag (IRAP)
		WriteUE(w, 0);	// slice_pic_parameter_set_id
		WriteUE(w, slice_type);	// slice_type (default 2 = I)
		if (sao_enabled)
		{
			w.WriteBits(1, 0);	// slice_sao_luma_flag
			w.WriteBits(1, 0);	// slice_sao_chroma_flag (ChromaArrayType != 0)
		}
		WriteSE(w, 0);	// slice_qp_delta
		// byte_alignment()
		w.WriteBits(1, 1);	// alignment_bit_equal_to_one
		while (w.GetBitCount() % 8 != 0)
		{
			w.WriteBits(1, 0);	// alignment_bit_equal_to_zero
		}
		auto rbsp = ToBytes(w);
		// A couple of bytes of (dummy) slice data after the header.
		rbsp.push_back(0xFF);
		rbsp.push_back(0xFF);
		return MakeNal(19, rbsp);  // IDR_W_RADL
	}

	std::shared_ptr<HEVCDecoderConfigurationRecord> BuildRecord(bool sao_enabled,
															   uint32_t num_strps = 0,
															   uint32_t num_extra_slice_header_bits = 0,
															   uint32_t log2_diff = 3,
															   bool range_extension = false,
															   bool entropy_coding_sync = false,
															   const SpsOverrides &overrides = {})
	{
		auto record = std::make_shared<HEVCDecoderConfigurationRecord>();
		record->AddNalUnit(H265NALUnitType::SPS, ToData(BuildSps(sao_enabled, num_strps, log2_diff, overrides)));
		record->AddNalUnit(H265NALUnitType::PPS, ToData(BuildPps(num_extra_slice_header_bits, range_extension,
																/*num_ref_idx_default_active_minus1=*/0, entropy_coding_sync)));
		return record;
	}

	// ---- Non-IDR (P) slice whose slice-level short-term RPS is inter-predicted
	// (short_term_ref_pic_set_sps_flag == 0, inter_ref_pic_set_prediction_flag == 1).
	// This is the case that exercises the ProcessShortTermRefPicSet flag loop; the
	// spec loop is inclusive (j <= NumDeltaPocs[RefRpsIdx]), so with NumDeltaPocs==1
	// the parser must read 2 used_by_curr_pic_flag bits. An off-by-one there shifts
	// every following bit and changes the measured header size.
	//
	// Built to require an SPS with one short-term RPS and a PPS with
	// num_extra_slice_header_bits == 5, so the total header (with the alignment bit)
	// is 25 bits -> 4 bytes. A one-bit under-read would give 24 bits -> 3 bytes.
	std::vector<uint8_t> BuildInterPredictedRpsPSlice(uint32_t delta_idx_minus1 = 0)
	{
		ov::BitWriter w(16);
		w.WriteBits(1, 1);	// first_slice_segment_in_pic_flag
		WriteUE(w, 0);	// slice_pic_parameter_set_id
		w.WriteBits(5, 0);	// slice_reserved_flag[0..4] (num_extra_slice_header_bits == 5)
		WriteUE(w, 1);	// slice_type (P)
		w.WriteBits(4, 0);	// slice_pic_order_cnt_lsb (log2_max_pic_order_cnt_lsb_minus4 + 4 == 4)
		w.WriteBits(1, 0);	// short_term_ref_pic_set_sps_flag -> inline st_ref_pic_set(1)

		// st_ref_pic_set(stRpsIdx == num_short_term_ref_pic_sets == 1): inter-predicted.
		w.WriteBits(1, 1);	// inter_ref_pic_set_prediction_flag
		WriteUE(w, delta_idx_minus1);	// delta_idx_minus1 (RefRpsIdx = idx - (delta_idx_minus1 + 1))
		w.WriteBits(1, 0);	// delta_rps_sign
		WriteUE(w, 0);	// abs_delta_rps_minus1
		// NumDeltaPocs[RefRpsIdx=0] == 1 -> loop j = 0..1 (inclusive): 2 flags.
		w.WriteBits(1, 1);	// used_by_curr_pic_flag[0] (used -> no use_delta_flag)
		w.WriteBits(1, 1);	// used_by_curr_pic_flag[1] (used -> no use_delta_flag)

		w.WriteBits(1, 0);	// num_ref_idx_active_override_flag
		WriteUE(w, 0);	// five_minus_max_num_merge_cand
		WriteSE(w, 0);	// slice_qp_delta

		// byte_alignment()
		w.WriteBits(1, 1);
		while (w.GetBitCount() % 8 != 0)
		{
			w.WriteBits(1, 0);
		}
		auto rbsp = ToBytes(w);
		rbsp.push_back(0xFF);
		rbsp.push_back(0xFF);
		return MakeNal(1, rbsp);  // TRAIL_R (non-IDR, non-IRAP)
	}

	// Non-IDR P slice that selects a short-term RPS from the SPS by index
	// (short_term_ref_pic_set_sps_flag == 1). short_term_ref_pic_set_idx is read as
	// Ceil(Log2(num_strps)) bits; pass an out-of-range value to hit the range guard.
	std::vector<uint8_t> BuildSpsRpsIdxPSlice(uint32_t num_strps, uint32_t idx)
	{
		ov::BitWriter w(16);
		w.WriteBits(1, 1);	// first_slice_segment_in_pic_flag
		WriteUE(w, 0);	// slice_pic_parameter_set_id
		WriteUE(w, 1);	// slice_type (P)
		w.WriteBits(4, 0);	// slice_pic_order_cnt_lsb (4 bits)
		w.WriteBits(1, 1);	// short_term_ref_pic_set_sps_flag
		if (num_strps > 1)
		{
			w.WriteBits(CeilLog2(num_strps), idx);	// short_term_ref_pic_set_idx
		}
		WriteTrailing(w);
		auto rbsp = ToBytes(w);
		rbsp.push_back(0xFF);
		return MakeNal(1, rbsp);  // TRAIL_R
	}

	// ---- Non-IDR B slice (7.3.6.1). Needs an SPS with exactly one short-term RPS.
	// 1 + 1 + 1 + 4 + 1 + 1 + 1 + 1 + 1 = 12 bits, + 1 alignment bit -> 2 bytes.
	std::vector<uint8_t> BuildBSlice()
	{
		ov::BitWriter w(16);
		w.WriteBits(1, 1);	// first_slice_segment_in_pic_flag
		WriteUE(w, 0);	// slice_pic_parameter_set_id
		WriteUE(w, 0);	// slice_type (0 = B)
		w.WriteBits(4, 0);	// slice_pic_order_cnt_lsb (log2_max_pic_order_cnt_lsb == 4)
		w.WriteBits(1, 1);	// short_term_ref_pic_set_sps_flag (num_strps == 1 -> no index)
		w.WriteBits(1, 0);	// num_ref_idx_active_override_flag
		w.WriteBits(1, 0);	// mvd_l1_zero_flag (B only)
		WriteUE(w, 0);	// five_minus_max_num_merge_cand
		WriteSE(w, 0);	// slice_qp_delta

		// byte_alignment()
		w.WriteBits(1, 1);
		while (w.GetBitCount() % 8 != 0)
		{
			w.WriteBits(1, 0);
		}
		auto rbsp = ToBytes(w);
		rbsp.push_back(0xFF);
		rbsp.push_back(0xFF);
		return MakeNal(1, rbsp);  // TRAIL_R
	}

	// ---- IDR slice carrying entry point offsets (WPP). Walks the
	// num_entry_point_offsets / offset_len_minus1 path (7.3.6.1).
	std::vector<uint8_t> BuildWppIdrSlice(uint32_t num_entry_point_offsets, uint32_t offset_len_minus1)
	{
		ov::BitWriter w(32);
		w.WriteBits(1, 1);	// first_slice_segment_in_pic_flag
		w.WriteBits(1, 0);	// no_output_of_prior_pics_flag (IRAP)
		WriteUE(w, 0);	// slice_pic_parameter_set_id
		WriteUE(w, 2);	// slice_type (I)
		WriteSE(w, 0);	// slice_qp_delta

		WriteUE(w, num_entry_point_offsets);
		if (num_entry_point_offsets > 0)
		{
			WriteUE(w, offset_len_minus1);
			// Offsets are written only for a representable width.
			if (offset_len_minus1 < 32)
			{
				for (uint32_t i = 0; i < num_entry_point_offsets; i++)
				{
					w.WriteBits(offset_len_minus1 + 1, 0);	// entry_point_offset_minus1[i]
				}
			}
		}

		// byte_alignment()
		w.WriteBits(1, 1);
		while (w.GetBitCount() % 8 != 0)
		{
			w.WriteBits(1, 0);
		}
		auto rbsp = ToBytes(w);
		rbsp.push_back(0xFF);
		rbsp.push_back(0xFF);
		return MakeNal(19, rbsp);  // IDR_W_RADL
	}

	// ---- Non-IDR P slice with long-term refs (7.3.6.1). Needs an SPS with
	// num_long_term_ref_pics_sps == 2, so lt_idx_sps is 1 bit. One entry comes from the
	// SPS and one is coded explicitly, walking both branches of the long-term loop.
	// 1 + 1 + 3 + 4 + 1 + 3 + 3 + (1+1) + (4+1+1) + 1 + 1 + 1 = 27 bits,
	// + 1 alignment bit -> 4 bytes.
	// num_long_term_ref_pics_sps selects the lt_idx_sps width, so it must match the SPS.
	std::vector<uint8_t> BuildLongTermRefPSlice(uint32_t num_long_term_sps = 1, uint32_t num_long_term_pics = 1,
											   uint32_t lt_idx_sps = 0, uint32_t num_long_term_ref_pics_sps = 2)
	{
		ov::BitWriter w(16);
		w.WriteBits(1, 1);	// first_slice_segment_in_pic_flag
		WriteUE(w, 0);	// slice_pic_parameter_set_id
		WriteUE(w, 1);	// slice_type (P)
		w.WriteBits(4, 0);	// slice_pic_order_cnt_lsb
		w.WriteBits(1, 1);	// short_term_ref_pic_set_sps_flag (num_strps == 1 -> no index)

		WriteUE(w, num_long_term_sps);	// num_long_term_sps (num_long_term_ref_pics_sps > 0 -> present)
		WriteUE(w, num_long_term_pics);	// num_long_term_pics

		// Entries are only written for counts small enough to be walked.
		const bool write_entries = (num_long_term_sps <= H265_MAX_DPB_SIZE && num_long_term_pics <= H265_MAX_DPB_SIZE);
		for (uint32_t i = 0; write_entries && i < num_long_term_sps + num_long_term_pics; i++)
		{
			if (i < num_long_term_sps)
			{
				// Taken from the SPS list; lt_idx_sps is present when the list has > 1 entry.
				if (num_long_term_ref_pics_sps > 1)
				{
					w.WriteBits(CeilLog2(num_long_term_ref_pics_sps), lt_idx_sps);
				}
			}
			else
			{
				// Coded explicitly.
				w.WriteBits(4, 0);	// poc_lsb_lt[i]
				w.WriteBits(1, 0);	// used_by_curr_pic_lt_flag[i]
			}
			w.WriteBits(1, 0);	// delta_poc_msb_present_flag[i]
		}

		w.WriteBits(1, 0);	// num_ref_idx_active_override_flag
		WriteUE(w, 0);	// five_minus_max_num_merge_cand
		WriteSE(w, 0);	// slice_qp_delta

		// byte_alignment()
		w.WriteBits(1, 1);
		while (w.GetBitCount() % 8 != 0)
		{
			w.WriteBits(1, 0);
		}
		auto rbsp = ToBytes(w);
		rbsp.push_back(0xFF);
		rbsp.push_back(0xFF);
		return MakeNal(1, rbsp);  // TRAIL_R
	}

	// ---- Non-first IDR slice segment that parses successfully (7.3.6.1).
	// slice_segment_address is u(Ceil(Log2(PicSizeInCtbsY))), so address_bits must match the
	// SPS; num_extra shifts the total off the byte boundary.
	std::vector<uint8_t> BuildNonFirstIdrSlice(uint32_t address_bits, uint32_t num_extra = 0)
	{
		ov::BitWriter w(16);
		w.WriteBits(1, 0);	// first_slice_segment_in_pic_flag
		w.WriteBits(1, 0);	// no_output_of_prior_pics_flag (IRAP)
		WriteUE(w, 0);	// slice_pic_parameter_set_id
		w.WriteBits(address_bits, 1);	// slice_segment_address
		for (uint32_t i = 0; i < num_extra; i++)
		{
			w.WriteBits(1, 0);	// slice_reserved_flag[i]
		}
		WriteUE(w, 2);	// slice_type (I)
		WriteSE(w, 0);	// slice_qp_delta

		// byte_alignment()
		w.WriteBits(1, 1);
		while (w.GetBitCount() % 8 != 0)
		{
			w.WriteBits(1, 0);
		}
		auto rbsp = ToBytes(w);
		rbsp.push_back(0xFF);
		rbsp.push_back(0xFF);
		return MakeNal(19, rbsp);  // IDR_W_RADL
	}

	// Non-first slice segment (first_slice_segment_in_pic_flag == 0). The parser must
	// read slice_segment_address = u(Ceil(Log2(PicSizeInCtbsY))); when PicSizeInCtbsY is
	// invalid (0) it fails fast before the address.
	std::vector<uint8_t> BuildNonFirstSlice()
	{
		ov::BitWriter w(16);
		w.WriteBits(1, 0);	// first_slice_segment_in_pic_flag
		WriteUE(w, 0);	// slice_pic_parameter_set_id
		WriteTrailing(w);
		auto rbsp = ToBytes(w);
		rbsp.push_back(0xFF);
		return MakeNal(1, rbsp);  // TRAIL_R
	}
}  // namespace

// ============================================================================
// SPS parsing
// ============================================================================

TEST(H265Parser, ParseCraftedSps)
{
	auto sps_nal = BuildSps(/*sao=*/false);

	H265SPS sps;
	ASSERT_TRUE(H265Parser::ParseSPS(sps_nal.data(), sps_nal.size(), sps));

	EXPECT_EQ(sps.GetWidth(), 320u);
	EXPECT_EQ(sps.GetHeight(), 240u);
	EXPECT_EQ(sps.GetChromaFormatIdc(), 1u);
	EXPECT_EQ(sps.GetChromaArrayType(), 1u);
	// CtbSizeY = 64 -> ceil(320/64)=5, ceil(240/64)=4 -> 20
	EXPECT_EQ(sps.GetPicSizeInCtbsY(), 20u);
}

// ============================================================================
// HEVCDecoderConfigurationRecord::GetSPS/GetPPS id range validation
// ============================================================================

TEST(H265DecoderConfig, GetSpsPpsLookupAndRange)
{
	auto record = BuildRecord(/*sao=*/false);

	// Present ids resolve.
	ASSERT_NE(record->GetSPS(0), nullptr);
	ASSERT_NE(record->GetPPS(0), nullptr);
	EXPECT_EQ(record->GetSPS(0)->GetWidth(), 320u);

	// Absent-but-valid ids -> nullptr.
	EXPECT_EQ(record->GetSPS(1), nullptr);
	EXPECT_EQ(record->GetPPS(1), nullptr);

	// Out-of-range ids must not wrap into the uint8_t map key.
	EXPECT_EQ(record->GetSPS(16), nullptr);	  // sps id range is [0, 15]
	EXPECT_EQ(record->GetSPS(256), nullptr);  // would wrap to 0
	EXPECT_EQ(record->GetSPS(-1), nullptr);
	EXPECT_EQ(record->GetPPS(64), nullptr);	  // pps id range is [0, 63]
	EXPECT_EQ(record->GetPPS(256), nullptr);  // would wrap to 0
	EXPECT_EQ(record->GetPPS(-1), nullptr);
}

TEST(H265DecoderConfig, CodecsParameterUsesHvc1)
{
	auto record = BuildRecord(/*sao=*/false);
	EXPECT_TRUE(record->GetCodecsParameter().HasPrefix("hvc1"));
}

// ============================================================================
// Slice header size accounting
// ============================================================================

TEST(H265Parser, IdrSliceHeaderSize_SaoOff)
{
	auto record = BuildRecord(/*sao=*/false);
	auto slice = BuildIdrSlice(/*sao=*/false);

	H265SliceHeader shd;
	ASSERT_TRUE(H265Parser::ParseSliceHeader(slice.data(), slice.size(), shd, record));
	EXPECT_EQ(shd.GetSliceType(), H265SliceHeader::SliceType::ISlice);
	// 7 header bits + 1 alignment bit = 8 bits -> 1 byte (NAL header excluded).
	EXPECT_EQ(shd.GetHeaderSizeInBytes(), 1u);
}

TEST(H265Parser, IdrSliceHeaderSize_SaoOn)
{
	auto record = BuildRecord(/*sao=*/true);
	auto slice = BuildIdrSlice(/*sao=*/true);

	H265SliceHeader shd;
	ASSERT_TRUE(H265Parser::ParseSliceHeader(slice.data(), slice.size(), shd, record));
	EXPECT_EQ(shd.GetSliceType(), H265SliceHeader::SliceType::ISlice);
	// 9 header bits + 1 alignment bit = 10 bits -> 2 bytes.
	EXPECT_EQ(shd.GetHeaderSizeInBytes(), 2u);
}

// ============================================================================
// Slice header guard / failure paths
// ============================================================================

TEST(H265Parser, SliceHeaderRejectsNullRecord)
{
	auto slice = BuildIdrSlice(/*sao=*/false);
	H265SliceHeader shd;
	EXPECT_FALSE(H265Parser::ParseSliceHeader(slice.data(), slice.size(), shd, nullptr));
}

TEST(H265Parser, SliceHeaderRejectsNonVclNal)
{
	auto record = BuildRecord(/*sao=*/false);
	auto sps_nal = BuildSps(/*sao=*/false);  // NAL type 33 is not a slice
	H265SliceHeader shd;
	EXPECT_FALSE(H265Parser::ParseSliceHeader(sps_nal.data(), sps_nal.size(), shd, record));
}

TEST(H265Parser, SliceHeaderRejectsMissingPps)
{
	// Record with only an SPS: the slice references PPS id 0 which is absent.
	auto record = std::make_shared<HEVCDecoderConfigurationRecord>();
	record->AddNalUnit(H265NALUnitType::SPS, ToData(BuildSps(/*sao=*/false)));

	auto slice = BuildIdrSlice(/*sao=*/false);
	H265SliceHeader shd;
	EXPECT_FALSE(H265Parser::ParseSliceHeader(slice.data(), slice.size(), shd, record));
}

// Regression guard for the ProcessShortTermRefPicSet inclusive-loop fix
// (Rec. ITU-T H.265 7.3.7: for j = 0; j <= NumDeltaPocs[RefRpsIdx]; j++).
// With the off-by-one (j < NumDeltaPocs) the parser under-reads one bit and
// the measured header size becomes 3 bytes instead of 4.
TEST(H265Parser, SliceHeaderSize_InlineInterPredictedRps)
{
	auto record = BuildRecord(/*sao=*/false, /*num_strps=*/1, /*num_extra=*/5);
	auto slice = BuildInterPredictedRpsPSlice();

	H265SliceHeader shd;
	ASSERT_TRUE(H265Parser::ParseSliceHeader(slice.data(), slice.size(), shd, record));
	EXPECT_EQ(shd.GetSliceType(), H265SliceHeader::SliceType::PSlice);
	// 24 header bits + 1 alignment bit = 25 bits -> 4 bytes.
	EXPECT_EQ(shd.GetHeaderSizeInBytes(), 4u);
}

TEST(H265Parser, BSliceHeaderSize)
{
	auto record = BuildRecord(/*sao=*/false, /*num_strps=*/1);
	auto slice = BuildBSlice();

	H265SliceHeader shd;
	ASSERT_TRUE(H265Parser::ParseSliceHeader(slice.data(), slice.size(), shd, record));
	EXPECT_EQ(shd.GetSliceType(), H265SliceHeader::SliceType::BSlice);
	// 12 header bits + 1 alignment bit = 13 bits -> 2 bytes.
	EXPECT_EQ(shd.GetHeaderSizeInBytes(), 2u);
}

// Two entry point offsets 8 bits wide:
// 1 + 1 + 1 + 3 + 1 + 3 + 7 + 2*8 = 33 bits, + 1 alignment bit -> 5 bytes.
TEST(H265Parser, WppSliceHeaderSize_EntryPointOffsets)
{
	auto record = BuildRecord(/*sao=*/false, /*num_strps=*/0, /*num_extra=*/0,
							  /*log2_diff=*/3, /*range_extension=*/false, /*entropy_coding_sync=*/true);
	auto slice = BuildWppIdrSlice(/*num_entry_point_offsets=*/2, /*offset_len_minus1=*/7);

	H265SliceHeader shd;
	ASSERT_TRUE(H265Parser::ParseSliceHeader(slice.data(), slice.size(), shd, record));
	EXPECT_EQ(shd.GetSliceType(), H265SliceHeader::SliceType::ISlice);
	EXPECT_EQ(shd.GetHeaderSizeInBytes(), 5u);
}

TEST(H265Parser, LongTermRefSliceHeaderSize)
{
	SpsOverrides overrides;
	overrides.long_term_ref_pics_present = true;
	overrides.num_long_term_ref_pics_sps = 2;

	auto record = BuildRecord(/*sao=*/false, /*num_strps=*/1, /*num_extra=*/0, /*log2_diff=*/3,
							  /*range_extension=*/false, /*entropy_coding_sync=*/false, overrides);
	auto slice = BuildLongTermRefPSlice();

	H265SliceHeader shd;
	ASSERT_TRUE(H265Parser::ParseSliceHeader(slice.data(), slice.size(), shd, record));
	EXPECT_EQ(shd.GetSliceType(), H265SliceHeader::SliceType::PSlice);
	// 27 header bits + 1 alignment bit = 28 bits -> 4 bytes.
	EXPECT_EQ(shd.GetHeaderSizeInBytes(), 4u);
}

// ============================================================================
// Fail-safe / robustness guards (validate the review fixes)
// ============================================================================

// slice_type must be 0..2; a larger value is rejected instead of mis-parsed.
TEST(H265Parser, SliceHeaderRejectsInvalidSliceType)
{
	auto record = BuildRecord(/*sao=*/false);
	auto slice = BuildIdrSlice(/*sao=*/false, /*slice_type=*/3);

	H265SliceHeader shd;
	EXPECT_FALSE(H265Parser::ParseSliceHeader(slice.data(), slice.size(), shd, record));
}

// short_term_ref_pic_set_idx read with Ceil(Log2(num)) bits can exceed num-1 when
// num is not a power of two (num=3 -> 2 bits -> value 3). It must be rejected.
TEST(H265Parser, SliceHeaderRejectsOutOfRangeStrpsIdx)
{
	auto record = BuildRecord(/*sao=*/false, /*num_strps=*/3);
	auto slice = BuildSpsRpsIdxPSlice(/*num_strps=*/3, /*idx=*/3);	 // valid range is 0..2

	H265SliceHeader shd;
	EXPECT_FALSE(H265Parser::ParseSliceHeader(slice.data(), slice.size(), shd, record));
}

// An inter-predicted inline RPS with delta_idx_minus1 >= stRpsIdx would underflow
// RefRpsIdx; the guard must reject it.
TEST(H265Parser, SliceHeaderRejectsInterRpsDeltaIdxUnderflow)
{
	auto record = BuildRecord(/*sao=*/false, /*num_strps=*/1, /*num_extra=*/5);
	// stRpsIdx == 1, so delta_idx_minus1 == 1 makes RefRpsIdx underflow.
	auto slice = BuildInterPredictedRpsPSlice(/*delta_idx_minus1=*/1);

	H265SliceHeader shd;
	EXPECT_FALSE(H265Parser::ParseSliceHeader(slice.data(), slice.size(), shd, record));
}

// CtbLog2SizeY out of the valid [4,6] range -> GetPicSizeInCtbsY() == 0, and a
// non-first slice (which needs slice_segment_address) must fail fast.
TEST(H265Parser, SliceHeaderRejectsInvalidCtbSize)
{
	H265SPS sps;
	auto sps_nal = BuildSps(/*sao=*/false, /*num_strps=*/0, /*log2_diff=*/4);  // CtbLog2SizeY = 7
	ASSERT_TRUE(H265Parser::ParseSPS(sps_nal.data(), sps_nal.size(), sps));
	EXPECT_EQ(sps.GetPicSizeInCtbsY(), 0u);

	auto record = BuildRecord(/*sao=*/false, /*num_strps=*/0, /*num_extra=*/0, /*log2_diff=*/4);
	auto slice = BuildNonFirstSlice();

	H265SliceHeader shd;
	EXPECT_FALSE(H265Parser::ParseSliceHeader(slice.data(), slice.size(), shd, record));
}

// A PPS that enables the range extension is not supported by the slice-header
// parser and must be rejected up front.
TEST(H265Parser, SliceHeaderRejectsRangeExtensionPps)
{
	auto record = BuildRecord(/*sao=*/false, /*num_strps=*/0, /*num_extra=*/0,
							  /*log2_diff=*/3, /*range_extension=*/true);
	auto slice = BuildIdrSlice(/*sao=*/false);

	H265SliceHeader shd;
	EXPECT_FALSE(H265Parser::ParseSliceHeader(slice.data(), slice.size(), shd, record));
}

// ============================================================================
// Range guards on Exp-Golomb values used as allocation sizes or loop bounds
// ============================================================================

TEST(H265Parser, SpsRejectsOversizedPictureDimensions)
{
	SpsOverrides overrides;
	overrides.pic_width = H265_MAX_PIC_DIMENSION_IN_LUMA_SAMPLES + 1;

	H265SPS sps;
	auto sps_nal = BuildSps(/*sao=*/false, /*num_strps=*/0, /*log2_diff=*/3, overrides);
	EXPECT_FALSE(H265Parser::ParseSPS(sps_nal.data(), sps_nal.size(), sps));

	// Rejected before the rounding and the product in GetPicSizeInCtbsY() can overflow.
	overrides.pic_width = 320;
	overrides.pic_height = 1u << 20;
	sps_nal = BuildSps(/*sao=*/false, /*num_strps=*/0, /*log2_diff=*/3, overrides);
	EXPECT_FALSE(H265Parser::ParseSPS(sps_nal.data(), sps_nal.size(), sps));
}

TEST(H265Parser, SpsRejectsZeroPictureDimensions)
{
	SpsOverrides overrides;
	overrides.pic_width = 0;

	H265SPS sps;
	auto sps_nal = BuildSps(/*sao=*/false, /*num_strps=*/0, /*log2_diff=*/3, overrides);
	EXPECT_FALSE(H265Parser::ParseSPS(sps_nal.data(), sps_nal.size(), sps));

	overrides.pic_width = 320;
	overrides.pic_height = 0;
	sps_nal = BuildSps(/*sao=*/false, /*num_strps=*/0, /*log2_diff=*/3, overrides);
	EXPECT_FALSE(H265Parser::ParseSPS(sps_nal.data(), sps_nal.size(), sps));
}

// num_short_term_ref_pic_sets is 0..64 (7.4.3.2.1); the slice header also does resize(n + 1).
TEST(H265Parser, SpsRejectsExcessiveShortTermRefPicSets)
{
	H265SPS sps;
	auto sps_nal = BuildSps(/*sao=*/false, /*num_strps=*/H265_MAX_NUM_SHORT_TERM_REF_PIC_SETS + 1);
	EXPECT_FALSE(H265Parser::ParseSPS(sps_nal.data(), sps_nal.size(), sps));

	// The boundary value is still accepted.
	sps_nal = BuildSps(/*sao=*/false, /*num_strps=*/H265_MAX_NUM_SHORT_TERM_REF_PIC_SETS);
	EXPECT_TRUE(H265Parser::ParseSPS(sps_nal.data(), sps_nal.size(), sps));
}

// num_negative_pics / num_positive_pics size four vectors in st_ref_pic_set() (7.4.8).
TEST(H265Parser, SpsRejectsExcessiveRefPicCounts)
{
	SpsOverrides overrides;
	overrides.num_negative_pics = H265_MAX_DPB_SIZE + 1;

	H265SPS sps;
	auto sps_nal = BuildSps(/*sao=*/false, /*num_strps=*/1, /*log2_diff=*/3, overrides);
	EXPECT_FALSE(H265Parser::ParseSPS(sps_nal.data(), sps_nal.size(), sps));

	// Each count is in range but their sum is not.
	overrides.num_negative_pics = 10;
	overrides.num_positive_pics = 10;
	sps_nal = BuildSps(/*sao=*/false, /*num_strps=*/1, /*log2_diff=*/3, overrides);
	EXPECT_FALSE(H265Parser::ParseSPS(sps_nal.data(), sps_nal.size(), sps));

	// The boundary sum is still accepted.
	overrides.num_negative_pics = H265_MAX_DPB_SIZE - 1;
	overrides.num_positive_pics = 1;
	sps_nal = BuildSps(/*sao=*/false, /*num_strps=*/1, /*log2_diff=*/3, overrides);
	EXPECT_TRUE(H265Parser::ParseSPS(sps_nal.data(), sps_nal.size(), sps));
}

// num_long_term_ref_pics_sps is 0..32 (7.4.3.2.1); resizes _used_by_curr_pic_lt_sps_flag.
TEST(H265Parser, SpsRejectsExcessiveLongTermRefPics)
{
	SpsOverrides overrides;
	overrides.long_term_ref_pics_present = true;
	overrides.num_long_term_ref_pics_sps = H265_MAX_NUM_LONG_TERM_REF_PICS_SPS + 1;

	H265SPS sps;
	auto sps_nal = BuildSps(/*sao=*/false, /*num_strps=*/0, /*log2_diff=*/3, overrides);
	EXPECT_FALSE(H265Parser::ParseSPS(sps_nal.data(), sps_nal.size(), sps));

	// The boundary value is still accepted.
	overrides.num_long_term_ref_pics_sps = H265_MAX_NUM_LONG_TERM_REF_PICS_SPS;
	sps_nal = BuildSps(/*sao=*/false, /*num_strps=*/0, /*log2_diff=*/3, overrides);
	EXPECT_TRUE(H265Parser::ParseSPS(sps_nal.data(), sps_nal.size(), sps));
}

// num_ref_idx_lX_default_active_minus1 is 0..14 (7.4.7.1); becomes the slice header default.
TEST(H265Parser, PpsRejectsExcessiveNumRefIdxDefaultActive)
{
	H265PPS pps;
	auto pps_nal = BuildPps(/*num_extra=*/0, /*range_extension=*/false,
							/*num_ref_idx_default_active_minus1=*/H265_MAX_NUM_REF_IDX_ACTIVE_MINUS1 + 1);
	EXPECT_FALSE(H265Parser::ParsePPS(pps_nal.data(), pps_nal.size(), pps));

	// The boundary value is still accepted.
	pps_nal = BuildPps(/*num_extra=*/0, /*range_extension=*/false,
					   /*num_ref_idx_default_active_minus1=*/H265_MAX_NUM_REF_IDX_ACTIVE_MINUS1);
	EXPECT_TRUE(H265Parser::ParsePPS(pps_nal.data(), pps_nal.size(), pps));
}

// slice_segment_address is u(Ceil(Log2(PicSizeInCtbsY))) and sizes the clear range of every
// non-first slice segment. 1920x1080 with CtbSizeY 64 gives 30*17 = 510 CTBs -> 9 bits.
// The two cases bracket the byte boundary so a one-bit error either way is caught.
TEST(H265Parser, NonFirstSliceSegmentHeaderSize)
{
	SpsOverrides overrides;
	overrides.pic_width = 1920;
	overrides.pic_height = 1080;

	{
		// 1 + 1 + 1 + 9 + 3 + 1 = 16 bits, + 1 alignment bit = 17 -> 3 bytes.
		// An 8-bit address would give 16 bits -> 2 bytes.
		auto record = BuildRecord(/*sao=*/false, /*num_strps=*/0, /*num_extra=*/0, /*log2_diff=*/3,
								  /*range_extension=*/false, /*entropy_coding_sync=*/false, overrides);
		ASSERT_EQ(record->GetSPS(0)->GetPicSizeInCtbsY(), 510u);

		auto slice = BuildNonFirstIdrSlice(/*address_bits=*/9);
		H265SliceHeader shd;
		ASSERT_TRUE(H265Parser::ParseSliceHeader(slice.data(), slice.size(), shd, record));
		EXPECT_EQ(shd.GetHeaderSizeInBytes(), 3u);
	}

	{
		// With 7 extra slice header bits: 16 + 7 = 23 bits, + 1 = 24 -> 3 bytes.
		// A 10-bit address would give 25 bits -> 4 bytes.
		auto record = BuildRecord(/*sao=*/false, /*num_strps=*/0, /*num_extra=*/7, /*log2_diff=*/3,
								  /*range_extension=*/false, /*entropy_coding_sync=*/false, overrides);

		auto slice = BuildNonFirstIdrSlice(/*address_bits=*/9, /*num_extra=*/7);
		H265SliceHeader shd;
		ASSERT_TRUE(H265Parser::ParseSliceHeader(slice.data(), slice.size(), shd, record));
		EXPECT_EQ(shd.GetHeaderSizeInBytes(), 3u);
	}
}

// A failed parse leaves the output struct cleared, not holding the previous slice's values.
TEST(H265Parser, SliceHeaderIsResetOnFailure)
{
	auto record = BuildRecord(/*sao=*/false);

	H265SliceHeader shd;
	auto good = BuildIdrSlice(/*sao=*/false);
	ASSERT_TRUE(H265Parser::ParseSliceHeader(good.data(), good.size(), shd, record));
	ASSERT_EQ(shd.GetHeaderSizeInBytes(), 1u);

	// Reusing the same struct for a NAL that fails must clear it.
	auto bad = BuildIdrSlice(/*sao=*/false, /*slice_type=*/3);
	EXPECT_FALSE(H265Parser::ParseSliceHeader(bad.data(), bad.size(), shd, record));
	EXPECT_EQ(shd.GetHeaderSizeInBytes(), 0u);
}

// ============================================================================
// Long-term reference picture counts and indices (7.4.7.1)
// ============================================================================

// num_long_term_sps + num_long_term_pics is summed in uint32 and drives the long-term loop.
TEST(H265Parser, SliceHeaderRejectsWrappingLongTermCount)
{
	SpsOverrides overrides;
	overrides.long_term_ref_pics_present = true;
	overrides.num_long_term_ref_pics_sps = 2;

	auto record = BuildRecord(/*sao=*/false, /*num_strps=*/1, /*num_extra=*/0, /*log2_diff=*/3,
							  /*range_extension=*/false, /*entropy_coding_sync=*/false, overrides);

	H265SliceHeader shd;
	// 0x80000000 + 0x80000000 wraps to 0.
	auto slice = BuildLongTermRefPSlice(/*num_long_term_sps=*/0x80000000, /*num_long_term_pics=*/0x80000000);
	EXPECT_FALSE(H265Parser::ParseSliceHeader(slice.data(), slice.size(), shd, record));

	// num_long_term_sps must not exceed the SPS list size.
	slice = BuildLongTermRefPSlice(/*num_long_term_sps=*/3, /*num_long_term_pics=*/0);
	EXPECT_FALSE(H265Parser::ParseSliceHeader(slice.data(), slice.size(), shd, record));

	// The sum must fit the DPB.
	slice = BuildLongTermRefPSlice(/*num_long_term_sps=*/2, /*num_long_term_pics=*/H265_MAX_DPB_SIZE);
	EXPECT_FALSE(H265Parser::ParseSliceHeader(slice.data(), slice.size(), shd, record));
}

// lt_idx_sps is read with Ceil(Log2(num_long_term_ref_pics_sps)) bits, so for a list of 3
// the value 3 is representable but out of range.
TEST(H265Parser, SliceHeaderRejectsOutOfRangeLtIdxSps)
{
	SpsOverrides overrides;
	overrides.long_term_ref_pics_present = true;
	overrides.num_long_term_ref_pics_sps = 3;	 // Ceil(Log2(3)) == 2 bits -> 0..3 representable

	auto record = BuildRecord(/*sao=*/false, /*num_strps=*/1, /*num_extra=*/0, /*log2_diff=*/3,
							  /*range_extension=*/false, /*entropy_coding_sync=*/false, overrides);

	H265SliceHeader shd;
	auto slice = BuildLongTermRefPSlice(/*num_long_term_sps=*/1, /*num_long_term_pics=*/0,
										/*lt_idx_sps=*/3, /*num_long_term_ref_pics_sps=*/3);
	EXPECT_FALSE(H265Parser::ParseSliceHeader(slice.data(), slice.size(), shd, record));

	// The last valid index is still accepted.
	slice = BuildLongTermRefPSlice(/*num_long_term_sps=*/1, /*num_long_term_pics=*/0,
								   /*lt_idx_sps=*/2, /*num_long_term_ref_pics_sps=*/3);
	EXPECT_TRUE(H265Parser::ParseSliceHeader(slice.data(), slice.size(), shd, record));
}

// ============================================================================
// Parameter set ids
// ============================================================================

// The record keys its SPS map by sps_seq_parameter_set_id, so it must be stored.
TEST(H265Parser, SpsIdIsParsedAndStored)
{
	H265SPS sps;
	auto sps_nal = BuildSps(/*sao=*/false, /*num_strps=*/0, /*log2_diff=*/3, /*overrides=*/{},
							/*log2_max_pic_order_cnt_lsb_minus4=*/0, /*sps_id=*/7);
	ASSERT_TRUE(H265Parser::ParseSPS(sps_nal.data(), sps_nal.size(), sps));
	EXPECT_EQ(sps.GetId(), 7u);
}

// Two SPS with different ids must both be retrievable.
TEST(H265DecoderConfig, DistinctSpsIdsCoexist)
{
	auto record = std::make_shared<HEVCDecoderConfigurationRecord>();
	record->AddNalUnit(H265NALUnitType::SPS, ToData(BuildSps(/*sao=*/false, 0, 3, {}, 0, /*sps_id=*/0)));
	record->AddNalUnit(H265NALUnitType::SPS, ToData(BuildSps(/*sao=*/false, 0, 3, {}, 0, /*sps_id=*/1)));

	ASSERT_NE(record->GetSPS(0), nullptr);
	ASSERT_NE(record->GetSPS(1), nullptr);
	EXPECT_EQ(record->GetSPS(0)->GetId(), 0u);
	EXPECT_EQ(record->GetSPS(1)->GetId(), 1u);
}

// A parameter set that reuses an id is an update: the stored parse and the hvcC arrays follow it.
TEST(H265DecoderConfig, SameSpsIdIsReplacedNotDropped)
{
	SpsOverrides wide;
	wide.pic_width = 1280;
	wide.pic_height = 720;

	auto record = std::make_shared<HEVCDecoderConfigurationRecord>();
	record->AddNalUnit(H265NALUnitType::SPS, ToData(BuildSps(/*sao=*/false)));	 // 320x240, id 0
	ASSERT_NE(record->GetSPS(0), nullptr);
	EXPECT_EQ(record->GetSPS(0)->GetWidth(), 320u);

	const auto nal_count_before = record->GetNalUnits(H265NALUnitType::SPS).size();

	// Same id, different resolution.
	record->AddNalUnit(H265NALUnitType::SPS, ToData(BuildSps(/*sao=*/false, 0, 3, wide)));

	ASSERT_NE(record->GetSPS(0), nullptr);
	EXPECT_EQ(record->GetSPS(0)->GetWidth(), 1280u);
	// The hvcC array holds one SPS for the id, not two.
	EXPECT_EQ(record->GetNalUnits(H265NALUnitType::SPS).size(), nal_count_before);
}

// GetVpsSpsPpsAsAnnexB() caches its result, so a later parameter set update must clear it.
TEST(H265DecoderConfig, AnnexBCacheFollowsSpsUpdate)
{
	SpsOverrides wide;
	wide.pic_width = 1280;
	wide.pic_height = 720;

	auto record = std::make_shared<HEVCDecoderConfigurationRecord>();
	record->AddNalUnit(H265NALUnitType::VPS, ToData(BuildVps()));
	record->AddNalUnit(H265NALUnitType::SPS, ToData(BuildSps(/*sao=*/false)));
	record->AddNalUnit(H265NALUnitType::PPS, ToData(BuildPps()));
	ASSERT_TRUE(record->IsValid());

	// Populate the cache.
	auto [first_data, first_frag] = record->GetVpsSpsPpsAsAnnexB();
	ASSERT_NE(first_data, nullptr);

	// Same id, larger SPS.
	auto wide_sps = BuildSps(/*sao=*/false, 0, 3, wide);
	record->AddNalUnit(H265NALUnitType::SPS, ToData(wide_sps));

	auto [second_data, second_frag] = record->GetVpsSpsPpsAsAnnexB();
	ASSERT_NE(second_data, nullptr);
	// The rebuilt Annex B must contain the new SPS, so it cannot be the cached buffer.
	EXPECT_NE(second_data->GetLength(), first_data->GetLength());
}

// GetData() in the base class caches the serialized record until UpdateData() is called, so a
// later parameter set update must mark it stale or WriteHvccBox() keeps writing the old bytes.
TEST(H265DecoderConfig, SerializedRecordFollowsSpsUpdate)
{
	SpsOverrides wide;
	wide.pic_width = 1280;
	wide.pic_height = 720;

	auto record = std::make_shared<HEVCDecoderConfigurationRecord>();
	record->AddNalUnit(H265NALUnitType::VPS, ToData(BuildVps()));
	record->AddNalUnit(H265NALUnitType::SPS, ToData(BuildSps(/*sao=*/false)));
	record->AddNalUnit(H265NALUnitType::PPS, ToData(BuildPps()));
	ASSERT_TRUE(record->IsValid());

	// Populate the cache.
	auto first = record->GetData();
	ASSERT_NE(first, nullptr);
	const auto first_length = first->GetLength();

	// Same id, larger SPS.
	record->AddNalUnit(H265NALUnitType::SPS, ToData(BuildSps(/*sao=*/false, 0, 3, wide)));

	auto second = record->GetData();
	ASSERT_NE(second, nullptr);
	EXPECT_NE(second->GetLength(), first_length);
}

// Ids out of the spec range would narrow into the uint8_t map key and alias a valid entry.
TEST(H265Parser, SpsRejectsOutOfRangeId)
{
	H265SPS sps;
	auto sps_nal = BuildSps(/*sao=*/false, 0, 3, {}, 0, /*sps_id=*/H265_MAX_SPS_ID + 1);
	EXPECT_FALSE(H265Parser::ParseSPS(sps_nal.data(), sps_nal.size(), sps));

	sps_nal = BuildSps(/*sao=*/false, 0, 3, {}, 0, /*sps_id=*/256);	 // would wrap to 0
	EXPECT_FALSE(H265Parser::ParseSPS(sps_nal.data(), sps_nal.size(), sps));

	sps_nal = BuildSps(/*sao=*/false, 0, 3, {}, 0, /*sps_id=*/H265_MAX_SPS_ID);
	EXPECT_TRUE(H265Parser::ParseSPS(sps_nal.data(), sps_nal.size(), sps));
}

TEST(H265Parser, PpsRejectsOutOfRangeId)
{
	H265PPS pps;
	auto pps_nal = BuildPps(0, false, 0, false, /*pps_id=*/H265_MAX_PPS_ID + 1);
	EXPECT_FALSE(H265Parser::ParsePPS(pps_nal.data(), pps_nal.size(), pps));

	pps_nal = BuildPps(0, false, 0, false, /*pps_id=*/256);	 // would wrap to 0
	EXPECT_FALSE(H265Parser::ParsePPS(pps_nal.data(), pps_nal.size(), pps));

	// pps_seq_parameter_set_id follows the SPS range, not the PPS range.
	pps_nal = BuildPps(0, false, 0, false, /*pps_id=*/0, /*sps_id=*/H265_MAX_SPS_ID + 1);
	EXPECT_FALSE(H265Parser::ParsePPS(pps_nal.data(), pps_nal.size(), pps));

	pps_nal = BuildPps(0, false, 0, false, /*pps_id=*/H265_MAX_PPS_ID, /*sps_id=*/H265_MAX_SPS_ID);
	EXPECT_TRUE(H265Parser::ParsePPS(pps_nal.data(), pps_nal.size(), pps));
}

// ============================================================================
// Values that reach ReadBits() as a bit count (uint8_t, so they can narrow)
// ============================================================================

// log2_max_pic_order_cnt_lsb_minus4 is 0..12 (7.4.3.2.1); value + 4 is used as a bit count.
TEST(H265Parser, SpsRejectsExcessiveLog2MaxPicOrderCntLsb)
{
	H265SPS sps;
	auto sps_nal = BuildSps(/*sao=*/false, /*num_strps=*/0, /*log2_diff=*/3, /*overrides=*/{},
							/*log2_max_pic_order_cnt_lsb_minus4=*/H265_MAX_LOG2_MAX_PIC_ORDER_CNT_LSB_MINUS4 + 1);
	EXPECT_FALSE(H265Parser::ParseSPS(sps_nal.data(), sps_nal.size(), sps));

	sps_nal = BuildSps(/*sao=*/false, /*num_strps=*/0, /*log2_diff=*/3, /*overrides=*/{},
					   /*log2_max_pic_order_cnt_lsb_minus4=*/252);
	EXPECT_FALSE(H265Parser::ParseSPS(sps_nal.data(), sps_nal.size(), sps));

	// The boundary value is still accepted.
	sps_nal = BuildSps(/*sao=*/false, /*num_strps=*/0, /*log2_diff=*/3, /*overrides=*/{},
					   /*log2_max_pic_order_cnt_lsb_minus4=*/H265_MAX_LOG2_MAX_PIC_ORDER_CNT_LSB_MINUS4);
	EXPECT_TRUE(H265Parser::ParseSPS(sps_nal.data(), sps_nal.size(), sps));
}

// offset_len_minus1 is 0..31 (7.4.7.1); at 255 the width narrows to 0 and consumes nothing.
TEST(H265Parser, SliceHeaderRejectsExcessiveOffsetLen)
{
	auto record = BuildRecord(/*sao=*/false, /*num_strps=*/0, /*num_extra=*/0,
							  /*log2_diff=*/3, /*range_extension=*/false, /*entropy_coding_sync=*/true);

	H265SliceHeader shd;
	auto slice = BuildWppIdrSlice(/*num_entry_point_offsets=*/2, /*offset_len_minus1=*/255);
	EXPECT_FALSE(H265Parser::ParseSliceHeader(slice.data(), slice.size(), shd, record));

	slice = BuildWppIdrSlice(/*num_entry_point_offsets=*/2, /*offset_len_minus1=*/H265_MAX_OFFSET_LEN_MINUS1 + 1);
	EXPECT_FALSE(H265Parser::ParseSliceHeader(slice.data(), slice.size(), shd, record));
}
