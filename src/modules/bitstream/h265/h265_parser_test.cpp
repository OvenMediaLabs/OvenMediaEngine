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
	uint32_t CeilLog2(uint32_t n)
	{
		uint32_t bits = 0;
		while ((1u << bits) < n)
		{
			bits++;
		}
		return bits;
	}

	// ---- Minimal SPS (4:2:0, 320x240, SAO configurable) ----
	// num_strps short-term RPS are stored, each with num_negative_pics=1
	// (delta_poc_s0_minus1=0, used_by_curr_pic_s0_flag=1), num_positive_pics=0
	// -> NumDeltaPocs == 1. log2_diff selects CtbLog2SizeY (= 3 + log2_diff).
	std::vector<uint8_t> BuildSps(bool sao_enabled, uint32_t num_strps = 0, uint32_t log2_diff = 3)
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

		WriteUE(w, 0);	// sps_seq_parameter_set_id
		WriteUE(w, 1);	// chroma_format_idc (4:2:0)
		WriteUE(w, 320);  // pic_width_in_luma_samples
		WriteUE(w, 240);  // pic_height_in_luma_samples
		w.WriteBits(1, 0);	// conformance_window_flag
		WriteUE(w, 0);	// bit_depth_luma_minus8
		WriteUE(w, 0);	// bit_depth_chroma_minus8
		WriteUE(w, 0);	// log2_max_pic_order_cnt_lsb_minus4

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
			WriteUE(w, 1);	// num_negative_pics
			WriteUE(w, 0);	// num_positive_pics
			WriteUE(w, 0);	// delta_poc_s0_minus1[0]
			w.WriteBits(1, 1);	// used_by_curr_pic_s0_flag[0]
		}
		w.WriteBits(1, 0);	// long_term_ref_pics_present_flag
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
	std::vector<uint8_t> BuildPps(uint32_t num_extra_slice_header_bits = 0, bool range_extension = false)
	{
		ov::BitWriter w(32);
		WriteUE(w, 0);	// pps_pic_parameter_set_id
		WriteUE(w, 0);	// pps_seq_parameter_set_id
		w.WriteBits(1, 0);	// dependent_slice_segments_enabled_flag
		w.WriteBits(1, 0);	// output_flag_present_flag
		w.WriteBits(3, num_extra_slice_header_bits);  // num_extra_slice_header_bits
		w.WriteBits(1, 0);	// sign_data_hiding_enabled_flag
		w.WriteBits(1, 0);	// cabac_init_present_flag
		WriteUE(w, 0);	// num_ref_idx_l0_default_active_minus1
		WriteUE(w, 0);	// num_ref_idx_l1_default_active_minus1
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
		w.WriteBits(1, 0);	// entropy_coding_sync_enabled_flag
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
															   bool range_extension = false)
	{
		auto record = std::make_shared<HEVCDecoderConfigurationRecord>();
		record->AddNalUnit(H265NALUnitType::SPS, ToData(BuildSps(sao_enabled, num_strps, log2_diff)));
		record->AddNalUnit(H265NALUnitType::PPS, ToData(BuildPps(num_extra_slice_header_bits, range_extension)));
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
