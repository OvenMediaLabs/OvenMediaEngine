//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2026 OvenMediaLabs. All rights reserved.
//
//==============================================================================
#include <gtest/gtest.h>

#include <base/ovlibrary/data.h>
#include <modules/bitstream/av1/av1_parser.h>

#include <cstring>
#include <vector>

namespace
{
	std::vector<uint8_t> EncodeLeb128(uint64_t value)
	{
		std::vector<uint8_t> bytes;
		do
		{
			uint8_t byte = value & 0x7F;
			value >>= 7;
			if (value != 0)
			{
				byte |= 0x80;
			}
			bytes.push_back(byte);
		} while (value != 0);
		return bytes;
	}

	uint8_t MakeObuHeaderByte(Av1ObuType type, bool extension_flag, bool has_size_field, uint8_t reserved_1bit = 0)
	{
		uint8_t b = 0;
		// `forbidden_bit(1)` | `type(4)` | `ext_flag(1)` | `has_size(1)` | `reserved(1)`
		b |= (static_cast<uint8_t>(type) & 0x0F) << 3;
		b |= (extension_flag ? 1 : 0) << 2;
		b |= (has_size_field ? 1 : 0) << 1;
		b |= (reserved_1bit & 0x01);
		return b;
	}

	uint8_t MakeObuExtensionByte(uint8_t temporal_id, uint8_t spatial_id, uint8_t reserved_3bits = 0)
	{
		uint8_t b = 0;
		b |= (temporal_id & 0x07) << 5;
		b |= (spatial_id & 0x03) << 3;
		b |= (reserved_3bits & 0x07);
		return b;
	}

	// Build a spec-correct sequence header OBU payload with the given top-level structural fields.
	// Always emits `frame_width_bits_minus_1 = 0`, `frame_height_bits_minus_1 = 0`,
	// `max_frame_width_minus_1` / `max_frame_height_minus_1` 1 bit each so the resulting `width`,
	// `height` come out as `width_minus_1 + 1` / `height_minus_1 + 1`.
	struct SeqHeaderBuilder
	{
		uint8_t seq_profile							  = 0;
		bool still_picture							  = false;
		bool reduced_still_picture_header			  = false;
		bool timing_info_present_flag				  = false;
		// `timing_info()` sub-fields (only used when `timing_info_present_flag`).
		uint32_t num_units_in_display_tick			  = 1;
		uint32_t time_scale							  = 30;
		bool equal_picture_interval					  = false;
		bool decoder_model_info_present_flag		  = false;
		uint8_t buffer_delay_length_minus_1			  = 0;
		bool initial_display_delay_present_flag		  = false;
		// Per-operating-point `initial_display_delay_present_for_this_op[i]` and
		// `initial_display_delay_minus_1[i]`. Only consulted when
		// `initial_display_delay_present_flag == true`. Defaults to all zeros which keeps the
		// builder backward-compatible with existing tests.
		std::vector<uint8_t> initial_display_delay_present_for_this_op;
		std::vector<uint8_t> initial_display_delay_minus_1;
		uint8_t operating_points_cnt_minus_1		  = 0;
		std::vector<uint16_t> operating_point_idc;
		std::vector<uint8_t> seq_level_idx;
		std::vector<uint8_t> seq_tier;	// only consulted when `seq_level_idx[i] > 7`
		uint8_t frame_width_bits_minus_1			  = 0;	// 1 bit
		uint8_t frame_height_bits_minus_1			  = 0;	// 1 bit
		uint32_t max_frame_width_minus_1			  = 0;
		uint32_t max_frame_height_minus_1			  = 0;

		// Spec-correct serialization of the post-`max_frame_height_minus_1` preamble (AV1 spec 5.5.1)
		// and the `color_config()` (AV1 spec 5.5.2) sub-tree. Defaults mirror the smallest-bitstream
		// path: no frame-id numbers, all enable_* flags 0, no screen-content tools, no jnt_comp, etc.
		bool frame_id_numbers_present_flag			  = false;
		uint8_t delta_frame_id_length_minus_2		  = 0;
		uint8_t additional_frame_id_length_minus_1	  = 0;
		uint8_t use_128x128_superblock				  = 0;
		uint8_t enable_filter_intra					  = 0;
		uint8_t enable_intra_edge_filter			  = 0;
		uint8_t enable_interintra_compound			  = 0;
		uint8_t enable_masked_compound				  = 0;
		uint8_t enable_warped_motion				  = 0;
		uint8_t enable_dual_filter					  = 0;
		uint8_t enable_order_hint					  = 0;
		uint8_t enable_jnt_comp						  = 0;
		uint8_t enable_ref_frame_mvs				  = 0;
		uint8_t seq_choose_screen_content_tools		  = 1;	// `SELECT_SCREEN_CONTENT_TOOLS` path - skips force flag
		uint8_t seq_force_screen_content_tools_when_chosen = 0;
		uint8_t seq_choose_integer_mv				  = 1;
		uint8_t seq_force_integer_mv_when_chosen	  = 0;
		uint8_t order_hint_bits_minus_1				  = 0;
		uint8_t enable_superres						  = 0;
		uint8_t enable_cdef							  = 0;
		uint8_t enable_restoration					  = 0;

		// AV1 spec 5.5.2 `color_config()` fields.
		uint8_t high_bitdepth						  = 0;
		uint8_t twelve_bit							  = 0;
		uint8_t monochrome							  = 0;
		uint8_t color_description_present_flag		  = 0;
		uint8_t color_primaries						  = 2;	// CP_UNSPECIFIED
		uint8_t transfer_characteristics			  = 2;	// TC_UNSPECIFIED
		uint8_t matrix_coefficients					  = 2;	// MC_UNSPECIFIED
		uint8_t color_range							  = 0;
		// 4:2:0 default for non-monochrome.
		uint8_t chroma_subsampling_x				  = 1;
		uint8_t chroma_subsampling_y				  = 1;
		uint8_t chroma_sample_position				  = 0;
	};

	std::vector<uint8_t> BuildSequenceHeaderObuPayload(const SeqHeaderBuilder &b)
	{
		ov::BitWriter w(64);
		w.WriteBits(3, b.seq_profile);
		w.WriteBits(1, b.still_picture ? 1 : 0);
		w.WriteBits(1, b.reduced_still_picture_header ? 1 : 0);
		if (b.reduced_still_picture_header)
		{
			// `seq_level_idx[0]` (5 bits) only.
			w.WriteBits(5, b.seq_level_idx.empty() ? 0 : b.seq_level_idx[0]);
		}
		else
		{
			w.WriteBits(1, b.timing_info_present_flag ? 1 : 0);
			if (b.timing_info_present_flag)
			{
				// AV1 spec 5.5.2 `timing_info()`
				w.WriteBits(32, b.num_units_in_display_tick);
				w.WriteBits(32, b.time_scale);
				w.WriteBits(1, b.equal_picture_interval ? 1 : 0);
				if (b.equal_picture_interval)
				{
					// uvlc value 0 -> single `1` bit.
					w.WriteBits(1, 1);
				}
				w.WriteBits(1, b.decoder_model_info_present_flag ? 1 : 0);
				if (b.decoder_model_info_present_flag)
				{
					// AV1 spec 5.5.3 `decoder_model_info()`
					w.WriteBits(5, b.buffer_delay_length_minus_1);
					w.WriteBits(32, 1u);  // num_units_in_decoding_tick
					w.WriteBits(5, 0u);   // buffer_removal_time_length_minus_1
					w.WriteBits(5, 0u);   // frame_presentation_time_length_minus_1
				}
			}
			w.WriteBits(1, b.initial_display_delay_present_flag ? 1 : 0);
			w.WriteBits(5, b.operating_points_cnt_minus_1);
			const uint32_t op_count = static_cast<uint32_t>(b.operating_points_cnt_minus_1) + 1;
			for (uint32_t i = 0; i < op_count; i++)
			{
				const uint16_t op_idc = (i < b.operating_point_idc.size()) ? b.operating_point_idc[i] : 0;
				const uint8_t lvl	  = (i < b.seq_level_idx.size()) ? b.seq_level_idx[i] : 0;
				w.WriteBits(12, op_idc);
				w.WriteBits(5, lvl);
				if (lvl > 7)
				{
					const uint8_t tier = (i < b.seq_tier.size()) ? b.seq_tier[i] : 0;
					w.WriteBits(1, tier);
				}
				if (b.decoder_model_info_present_flag)
				{
					// `decoder_model_present_for_this_op[i] = 0` for the test fixture.
					w.WriteBits(1, 0);
				}
				if (b.initial_display_delay_present_flag)
				{
					const uint8_t op_present = (i < b.initial_display_delay_present_for_this_op.size())
												   ? b.initial_display_delay_present_for_this_op[i]
												   : 0;
					w.WriteBits(1, op_present);
					if (op_present != 0)
					{
						const uint8_t op_delay = (i < b.initial_display_delay_minus_1.size())
													 ? b.initial_display_delay_minus_1[i]
													 : 0;
						w.WriteBits(4, op_delay & 0x0F);
					}
				}
			}
		}
		w.WriteBits(4, b.frame_width_bits_minus_1);
		w.WriteBits(4, b.frame_height_bits_minus_1);
		const uint8_t width_bits  = static_cast<uint8_t>(b.frame_width_bits_minus_1 + 1);
		const uint8_t height_bits = static_cast<uint8_t>(b.frame_height_bits_minus_1 + 1);
		w.WriteBits(width_bits, b.max_frame_width_minus_1);
		w.WriteBits(height_bits, b.max_frame_height_minus_1);

		// AV1 spec 5.5.1 continuation past `max_frame_height_minus_1`.
		if (b.reduced_still_picture_header == false)
		{
			w.WriteBits(1, b.frame_id_numbers_present_flag ? 1 : 0);
			if (b.frame_id_numbers_present_flag)
			{
				w.WriteBits(4, b.delta_frame_id_length_minus_2);
				w.WriteBits(3, b.additional_frame_id_length_minus_1);
			}
		}
		w.WriteBits(1, b.use_128x128_superblock);
		w.WriteBits(1, b.enable_filter_intra);
		w.WriteBits(1, b.enable_intra_edge_filter);
		if (b.reduced_still_picture_header == false)
		{
			w.WriteBits(1, b.enable_interintra_compound);
			w.WriteBits(1, b.enable_masked_compound);
			w.WriteBits(1, b.enable_warped_motion);
			w.WriteBits(1, b.enable_dual_filter);
			w.WriteBits(1, b.enable_order_hint);
			if (b.enable_order_hint)
			{
				w.WriteBits(1, b.enable_jnt_comp);
				w.WriteBits(1, b.enable_ref_frame_mvs);
			}
			w.WriteBits(1, b.seq_choose_screen_content_tools);
			uint8_t seq_force_screen_content_tools = 2;	 // SELECT_SCREEN_CONTENT_TOOLS
			if (b.seq_choose_screen_content_tools == 0)
			{
				w.WriteBits(1, b.seq_force_screen_content_tools_when_chosen);
				seq_force_screen_content_tools = b.seq_force_screen_content_tools_when_chosen;
			}
			if (seq_force_screen_content_tools > 0)
			{
				w.WriteBits(1, b.seq_choose_integer_mv);
				if (b.seq_choose_integer_mv == 0)
				{
					w.WriteBits(1, b.seq_force_integer_mv_when_chosen);
				}
			}
			if (b.enable_order_hint)
			{
				w.WriteBits(3, b.order_hint_bits_minus_1);
			}
		}
		w.WriteBits(1, b.enable_superres);
		w.WriteBits(1, b.enable_cdef);
		w.WriteBits(1, b.enable_restoration);

		// AV1 spec 5.5.2 `color_config()`.
		w.WriteBits(1, b.high_bitdepth);
		if ((b.seq_profile == 2) && (b.high_bitdepth != 0))
		{
			w.WriteBits(1, b.twelve_bit);
		}
		if (b.seq_profile != 1)
		{
			w.WriteBits(1, b.monochrome);
		}
		w.WriteBits(1, b.color_description_present_flag);
		if (b.color_description_present_flag)
		{
			w.WriteBits(8, b.color_primaries);
			w.WriteBits(8, b.transfer_characteristics);
			w.WriteBits(8, b.matrix_coefficients);
		}
		const bool srgb_shortcut = (b.monochrome == 0) &&
								   (b.color_primaries == 1) &&
								   (b.transfer_characteristics == 13) &&
								   (b.matrix_coefficients == 0);
		if (b.monochrome != 0)
		{
			w.WriteBits(1, b.color_range);
		}
		else if (srgb_shortcut)
		{
			// color_range, subsampling inferred per spec; no bits written.
		}
		else
		{
			w.WriteBits(1, b.color_range);
			if (b.seq_profile == 2)
			{
				const uint8_t bit_depth = (b.high_bitdepth != 0) ? ((b.twelve_bit != 0) ? 12 : 10) : 8;
				if (bit_depth == 12)
				{
					w.WriteBits(1, b.chroma_subsampling_x);
					if (b.chroma_subsampling_x != 0)
					{
						w.WriteBits(1, b.chroma_subsampling_y);
					}
				}
			}
			// For seq_profile 0 / 1 subsampling is implicit per spec.
			uint8_t eff_ss_x = b.chroma_subsampling_x;
			uint8_t eff_ss_y = b.chroma_subsampling_y;
			if (b.seq_profile == 0)
			{
				eff_ss_x = 1;
				eff_ss_y = 1;
			}
			else if (b.seq_profile == 1)
			{
				eff_ss_x = 0;
				eff_ss_y = 0;
			}
			else if (b.seq_profile == 2)
			{
				const uint8_t bit_depth = (b.high_bitdepth != 0) ? ((b.twelve_bit != 0) ? 12 : 10) : 8;
				if (bit_depth != 12)
				{
					eff_ss_x = 1;
					eff_ss_y = 0;
				}
			}
			if ((eff_ss_x != 0) && (eff_ss_y != 0))
			{
				w.WriteBits(2, b.chroma_sample_position);
			}
		}

		return std::vector<uint8_t>(w.GetData(), w.GetData() + w.GetDataSize());
	}

	std::vector<uint8_t> MakeObu(Av1ObuType type, const std::vector<uint8_t> &payload, bool has_size_field = true)
	{
		std::vector<uint8_t> bytes;
		bytes.push_back(MakeObuHeaderByte(type, false, has_size_field));
		if (has_size_field)
		{
			auto leb = EncodeLeb128(payload.size());
			bytes.insert(bytes.end(), leb.begin(), leb.end());
		}
		bytes.insert(bytes.end(), payload.begin(), payload.end());
		return bytes;
	}
}  // namespace

TEST(Av1ParserLeb128, RoundTripBoundaries)
{
	const uint64_t values[] = {0, 0x7F, 0x80, 0x3FFF, 0x4000, 0x1FFFFF, 0x200000};
	for (uint64_t v : values)
	{
		auto enc = EncodeLeb128(v);
		auto decoded = Av1Parser::DecodeLeb128(enc.data(), enc.size());
		ASSERT_TRUE(decoded.has_value()) << "value=" << v;
		EXPECT_EQ(decoded->value, v);
		EXPECT_EQ(decoded->bytes_consumed, enc.size());
	}
}

TEST(Av1ParserLeb128, RejectsTruncatedContinuation)
{
	const uint8_t bytes[] = {0x80};  // continuation bit (`0x80`) set but no follow byte
	EXPECT_FALSE(Av1Parser::DecodeLeb128(bytes, sizeof(bytes)).has_value());
}

TEST(Av1ParserLeb128, RejectsOverlong)
{
	uint8_t bytes[9];
	std::memset(bytes, 0x80, sizeof(bytes));
	EXPECT_FALSE(Av1Parser::DecodeLeb128(bytes, sizeof(bytes)).has_value());
}

TEST(Av1ParserObuHeader, SequenceHeaderNoExtensionNoSize)
{
	const uint8_t bytes[] = {MakeObuHeaderByte(Av1ObuType::SequenceHeader, false, false)};
	auto parsed = Av1Parser::ParseObuHeader(bytes, sizeof(bytes));
	ASSERT_TRUE(parsed.has_value());
	EXPECT_EQ(parsed->header.type, Av1ObuType::SequenceHeader);
	EXPECT_FALSE(parsed->header.extension_flag);
	EXPECT_FALSE(parsed->header.has_size_field);
	EXPECT_EQ(parsed->bytes_consumed, 1u);
}

TEST(Av1ParserObuHeader, TemporalDelimiter)
{
	const uint8_t bytes[] = {MakeObuHeaderByte(Av1ObuType::TemporalDelimiter, false, true)};
	auto parsed = Av1Parser::ParseObuHeader(bytes, sizeof(bytes));
	ASSERT_TRUE(parsed.has_value());
	EXPECT_EQ(parsed->header.type, Av1ObuType::TemporalDelimiter);
	EXPECT_TRUE(parsed->header.has_size_field);
	EXPECT_EQ(parsed->bytes_consumed, 1u);
}

TEST(Av1ParserObuHeader, FrameWithSizeField)
{
	const uint8_t bytes[] = {MakeObuHeaderByte(Av1ObuType::Frame, false, true)};
	auto parsed = Av1Parser::ParseObuHeader(bytes, sizeof(bytes));
	ASSERT_TRUE(parsed.has_value());
	EXPECT_EQ(parsed->header.type, Av1ObuType::Frame);
	EXPECT_FALSE(parsed->header.extension_flag);
	EXPECT_TRUE(parsed->header.has_size_field);
}

TEST(Av1ParserObuHeader, ExtensionByteCarriesTemporalAndSpatialId)
{
	const uint8_t bytes[] = {
		MakeObuHeaderByte(Av1ObuType::Frame, true, true),
		MakeObuExtensionByte(5, 2),
	};
	auto parsed = Av1Parser::ParseObuHeader(bytes, sizeof(bytes));
	ASSERT_TRUE(parsed.has_value());
	EXPECT_EQ(parsed->header.type, Av1ObuType::Frame);
	EXPECT_TRUE(parsed->header.extension_flag);
	EXPECT_EQ(parsed->header.temporal_id, 5u);
	EXPECT_EQ(parsed->header.spatial_id, 2u);
	EXPECT_EQ(parsed->bytes_consumed, 2u);
}

TEST(Av1ParserObuHeader, RejectsForbiddenBitSet)
{
	// `forbidden_bit(1)=1`
	const uint8_t bytes[] = {static_cast<uint8_t>(0x80 | MakeObuHeaderByte(Av1ObuType::SequenceHeader, false, false))};
	EXPECT_FALSE(Av1Parser::ParseObuHeader(bytes, sizeof(bytes)).has_value());
}

TEST(Av1ParserObuHeader, RejectsTruncatedExtensionByte)
{
	const uint8_t bytes[] = {MakeObuHeaderByte(Av1ObuType::Frame, true, false)};
	EXPECT_FALSE(Av1Parser::ParseObuHeader(bytes, sizeof(bytes)).has_value());
}

TEST(Av1ParserExtractSequenceHeader, ReturnsFirstSequenceHeaderPayload)
{
	const std::vector<uint8_t> td_payload = {};
	const std::vector<uint8_t> seq_payload = {0x11, 0x22, 0x33, 0x44, 0x55};
	const std::vector<uint8_t> fh_payload = {0xAA, 0xBB};

	std::vector<uint8_t> blob;
	auto td = MakeObu(Av1ObuType::TemporalDelimiter, td_payload);
	auto seq = MakeObu(Av1ObuType::SequenceHeader, seq_payload);
	auto fh = MakeObu(Av1ObuType::FrameHeader, fh_payload);
	blob.insert(blob.end(), td.begin(), td.end());
	blob.insert(blob.end(), seq.begin(), seq.end());
	blob.insert(blob.end(), fh.begin(), fh.end());

	auto data = std::make_shared<ov::Data>(blob.data(), blob.size());
	auto extracted = Av1Parser::ExtractFirstSequenceHeaderObu(data);
	ASSERT_NE(extracted, nullptr);
	ASSERT_EQ(extracted->GetLength(), seq_payload.size());
	EXPECT_EQ(std::memcmp(extracted->GetDataAs<uint8_t>(), seq_payload.data(), seq_payload.size()), 0);
}

TEST(Av1ParserExtractSequenceHeader, ReturnsNullWhenAbsent)
{
	const std::vector<uint8_t> td_payload = {};
	const std::vector<uint8_t> fh_payload = {0xAA, 0xBB};

	std::vector<uint8_t> blob;
	auto td = MakeObu(Av1ObuType::TemporalDelimiter, td_payload);
	auto fh = MakeObu(Av1ObuType::FrameHeader, fh_payload);
	blob.insert(blob.end(), td.begin(), td.end());
	blob.insert(blob.end(), fh.begin(), fh.end());

	auto data = std::make_shared<ov::Data>(blob.data(), blob.size());
	auto extracted = Av1Parser::ExtractFirstSequenceHeaderObu(data);
	EXPECT_EQ(extracted, nullptr);
}

TEST(Av1ParserExtractSequenceHeader, ReturnsNullOnTruncatedSizeField)
{
	std::vector<uint8_t> blob;
	blob.push_back(MakeObuHeaderByte(Av1ObuType::SequenceHeader, false, true));
	blob.push_back(0x80);  // `LEB128` says "more bytes follow" but none do.
	auto data = std::make_shared<ov::Data>(blob.data(), blob.size());
	EXPECT_EQ(Av1Parser::ExtractFirstSequenceHeaderObu(data), nullptr);
}

TEST(Av1ParserObuHeader, RejectsReservedBitNonZero)
{
	// `obu_reserved_1bit = 1` violates AV1 spec 5.3.2.
	const uint8_t bytes[] = {MakeObuHeaderByte(Av1ObuType::SequenceHeader, false, false, 1)};
	EXPECT_FALSE(Av1Parser::ParseObuHeader(bytes, sizeof(bytes)).has_value());
}

TEST(Av1ParserObuHeader, RejectsExtensionReservedBitsNonZero)
{
	// `extension_header_reserved_3bits != 0` violates AV1 spec 5.3.3.
	const uint8_t bytes[] = {
		MakeObuHeaderByte(Av1ObuType::Frame, true, true),
		MakeObuExtensionByte(1, 0, 0x01),
	};
	EXPECT_FALSE(Av1Parser::ParseObuHeader(bytes, sizeof(bytes)).has_value());
}

TEST(Av1ParserSequenceHeaderSummary, ParsesReducedStillPictureHeader)
{
	// `seq_profile=0` (3), `still_picture=1` (1), `reduced_still_picture_header=1` (1),
	// `seq_level_idx_0=5` (5), `frame_width_bits_minus_1=7` (4 -> 8 bits),
	// `frame_height_bits_minus_1=7` (4 -> 8 bits), `max_frame_width_minus_1=0x4F` (8 -> 80),
	// `max_frame_height_minus_1=0x2F` (8 -> 48). Then byte-align padding.
	//
	// Bit layout:
	//   `[000][1][1][00101][0111][0111][01001111][00101111] ...`
	// Build with a bit writer for clarity.
	ov::BitWriter bw(64);
	bw.WriteBits(3, 0);   // seq_profile
	bw.WriteBits(1, 1);   // still_picture
	bw.WriteBits(1, 1);   // reduced_still_picture_header
	bw.WriteBits(5, 5);   // seq_level_idx_0
	bw.WriteBits(4, 7);   // frame_width_bits_minus_1
	bw.WriteBits(4, 7);   // frame_height_bits_minus_1
	bw.WriteBits(8, 79);  // max_frame_width_minus_1 -> width 80
	bw.WriteBits(8, 47);  // max_frame_height_minus_1 -> height 48
	// AV1 spec 5.5.1 continuation: reduced_still_picture_header branch skips frame-id, jumps directly
	// to use_128x128_superblock / enable_filter_intra / enable_intra_edge_filter / enable_superres /
	// enable_cdef / enable_restoration, then `color_config()`.
	bw.WriteBits(1, 0);   // use_128x128_superblock
	bw.WriteBits(1, 0);   // enable_filter_intra
	bw.WriteBits(1, 0);   // enable_intra_edge_filter
	bw.WriteBits(1, 0);   // enable_superres
	bw.WriteBits(1, 0);   // enable_cdef
	bw.WriteBits(1, 0);   // enable_restoration
	bw.WriteBits(1, 0);   // high_bitdepth
	bw.WriteBits(1, 0);   // monochrome
	bw.WriteBits(1, 0);   // color_description_present_flag
	bw.WriteBits(1, 0);   // color_range
	bw.WriteBits(2, 0);   // chroma_sample_position (profile 0 -> subsampling 1,1 implicit)

	auto bytes = bw.GetData();
	auto summary = Av1Parser::ParseSequenceHeaderSummary(bytes, bw.GetDataSize());
	ASSERT_TRUE(summary.has_value());
	EXPECT_TRUE(summary->parsed);
	EXPECT_EQ(summary->seq_profile, 0);
	EXPECT_TRUE(summary->still_picture);
	EXPECT_TRUE(summary->reduced_still_picture_header);
	EXPECT_EQ(summary->seq_level_idx_0, 5);
	EXPECT_EQ(summary->max_frame_width, 80u);
	EXPECT_EQ(summary->max_frame_height, 48u);
}

TEST(Av1ParserSequenceHeaderSummary, ParsesFullSequenceHeaderNoTimingInfo)
{
	// `reduced_still_picture_header == 0` happy path: one operating point, no timing info,
	// no decoder model, no initial display delay. Verifies bit offset alignment of the
	// `12 + 5 = 17` per-op bits matches the previously hand-rolled "fast path".
	SeqHeaderBuilder b;
	b.seq_profile				   = 1;
	b.timing_info_present_flag	   = false;
	b.operating_points_cnt_minus_1 = 0;
	b.operating_point_idc		   = {0};
	b.seq_level_idx				   = {6};
	b.frame_width_bits_minus_1	   = 9;	 // 10 bits
	b.frame_height_bits_minus_1	   = 8;	 // 9 bits
	b.max_frame_width_minus_1	   = 639;
	b.max_frame_height_minus_1	   = 479;

	auto payload = BuildSequenceHeaderObuPayload(b);
	auto summary = Av1Parser::ParseSequenceHeaderSummary(payload.data(), payload.size());
	ASSERT_TRUE(summary.has_value());
	EXPECT_FALSE(summary->reduced_still_picture_header);
	EXPECT_EQ(summary->seq_profile, 1);
	EXPECT_EQ(summary->seq_level_idx_0, 6);
	EXPECT_EQ(summary->max_frame_width, 640u);
	EXPECT_EQ(summary->max_frame_height, 480u);
}

TEST(Av1ParserSequenceHeaderSummary, ParsesTimingInfoPresent)
{
	// AV1 ISOBMFF binding allows `timing_info_present_flag == 1`. The summary parser must
	// consume the `timing_info()` sub-tree and still recover width/height correctly.
	SeqHeaderBuilder b;
	b.seq_profile				   = 0;
	b.timing_info_present_flag	   = true;
	b.num_units_in_display_tick	   = 1001;
	b.time_scale				   = 60000;
	b.equal_picture_interval	   = false;
	b.operating_points_cnt_minus_1 = 0;
	b.operating_point_idc		   = {0};
	b.seq_level_idx				   = {4};
	b.frame_width_bits_minus_1	   = 10;
	b.frame_height_bits_minus_1	   = 10;
	b.max_frame_width_minus_1	   = 1919;
	b.max_frame_height_minus_1	   = 1079;

	auto payload = BuildSequenceHeaderObuPayload(b);
	auto summary = Av1Parser::ParseSequenceHeaderSummary(payload.data(), payload.size());
	ASSERT_TRUE(summary.has_value());
	EXPECT_EQ(summary->seq_level_idx_0, 4);
	EXPECT_EQ(summary->max_frame_width, 1920u);
	EXPECT_EQ(summary->max_frame_height, 1080u);
}

TEST(Av1ParserSequenceHeaderSummary, ParsesSeqTierBranch)
{
	// `seq_level_idx[0] > 7` triggers the `seq_tier[0]` bit per AV1 spec 5.5.1.
	SeqHeaderBuilder b;
	b.seq_profile				   = 0;
	b.operating_points_cnt_minus_1 = 0;
	b.operating_point_idc		   = {0};
	b.seq_level_idx				   = {8};
	b.seq_tier					   = {1};
	b.frame_width_bits_minus_1	   = 10;
	b.frame_height_bits_minus_1	   = 10;
	b.max_frame_width_minus_1	   = 1919;
	b.max_frame_height_minus_1	   = 1079;

	auto payload = BuildSequenceHeaderObuPayload(b);
	auto summary = Av1Parser::ParseSequenceHeaderSummary(payload.data(), payload.size());
	ASSERT_TRUE(summary.has_value());
	EXPECT_EQ(summary->seq_level_idx_0, 8);
	EXPECT_EQ(summary->max_frame_width, 1920u);
	EXPECT_EQ(summary->max_frame_height, 1080u);
}

TEST(Av1ParserSequenceHeaderSummary, ParsesMultipleOperatingPoints)
{
	// `operating_points_cnt_minus_1 = 1` -> 2 operating points. Only `i == 0` `seq_level_idx`
	// is recorded; the parser must still consume the extra op-point bits exactly.
	SeqHeaderBuilder b;
	b.seq_profile				   = 0;
	b.operating_points_cnt_minus_1 = 1;
	b.operating_point_idc		   = {0x1FF, 0x0FF};
	b.seq_level_idx				   = {6, 9};
	b.seq_tier					   = {0, 1};
	b.frame_width_bits_minus_1	   = 10;
	b.frame_height_bits_minus_1	   = 10;
	b.max_frame_width_minus_1	   = 1919;
	b.max_frame_height_minus_1	   = 1079;

	auto payload = BuildSequenceHeaderObuPayload(b);
	auto summary = Av1Parser::ParseSequenceHeaderSummary(payload.data(), payload.size());
	ASSERT_TRUE(summary.has_value());
	EXPECT_EQ(summary->seq_level_idx_0, 6);
	EXPECT_EQ(summary->max_frame_width, 1920u);
	EXPECT_EQ(summary->max_frame_height, 1080u);
}

TEST(Av1ParserSequenceHeaderSummary, RejectsTruncatedTimingInfo)
{
	// `timing_info_present_flag = 1` but only 1 byte of payload after the leading bits ->
	// `timing_info()` cannot be consumed, parser must fail cleanly.
	ov::BitWriter bw(8);
	bw.WriteBits(3, 0);	 // seq_profile
	bw.WriteBits(1, 0);	 // still_picture
	bw.WriteBits(1, 0);	 // reduced_still_picture_header
	bw.WriteBits(1, 1);	 // timing_info_present_flag
	bw.WriteBits(2, 0);	 // pad to byte boundary
	auto payload = bw.GetData();
	auto summary = Av1Parser::ParseSequenceHeaderSummary(payload, bw.GetDataSize());
	EXPECT_FALSE(summary.has_value());
}

TEST(Av1ParserObuHeader, RejectsReservedObuType)
{
	// AV1 spec 5.3.2 Table 5: obu_type values 0 and 9..14 are reserved. Per the spec,
	// "It is a requirement of bitstream conformance that the value of obu_type is not equal to one
	// of the reserved values."
	for (uint8_t reserved : {0, 9, 10, 11, 12, 13, 14})
	{
		// `forbidden_bit(1)=0`, `obu_type(4)=reserved`, `ext_flag(1)=0`, `has_size(1)=0`, `reserved(1)=0`
		const uint8_t byte = static_cast<uint8_t>((reserved & 0x0F) << 3);
		EXPECT_FALSE(Av1Parser::ParseObuHeader(&byte, 1).has_value())
			<< "obu_type=" << static_cast<int>(reserved);
	}
}

TEST(Av1ParserSequenceHeaderSummary, RejectsReducedStillPictureWithoutStillPicture)
{
	// AV1 spec 5.5.1: "It is a requirement of bitstream conformance that if
	// reduced_still_picture_header is equal to 1, the value of still_picture shall be equal to 1."
	ov::BitWriter bw(8);
	bw.WriteBits(3, 0);   // seq_profile
	bw.WriteBits(1, 0);   // still_picture = 0
	bw.WriteBits(1, 1);   // reduced_still_picture_header = 1 (mismatch)
	bw.WriteBits(5, 0);   // seq_level_idx_0
	bw.WriteBits(2, 0);   // pad
	auto payload = bw.GetData();
	auto summary = Av1Parser::ParseSequenceHeaderSummary(payload, bw.GetDataSize());
	EXPECT_FALSE(summary.has_value());
}

TEST(Av1ParserExtractSequenceHeader, HandlesTemporalDelimiterWithoutSizeField)
{
	// AV1 spec 5.6 `temporal_delimiter_obu()`: empty payload. When the stream uses the low-overhead
	// bitstream format (`obu_has_size_field = 0`), the in-band scan must walk past the leading
	// TemporalDelimiter (0-byte payload) and still find a subsequent SequenceHeader OBU.
	std::vector<uint8_t> blob;
	// TemporalDelimiter, has_size_field = 0.
	blob.push_back(MakeObuHeaderByte(Av1ObuType::TemporalDelimiter, false, false));
	// SequenceHeader with has_size_field = 1 to delimit the payload.
	const std::vector<uint8_t> seq_payload = {0xAB, 0xCD, 0xEF};
	auto seq = MakeObu(Av1ObuType::SequenceHeader, seq_payload, true);
	blob.insert(blob.end(), seq.begin(), seq.end());

	auto data	   = std::make_shared<ov::Data>(blob.data(), blob.size());
	auto extracted = Av1Parser::ExtractFirstSequenceHeaderObu(data);
	ASSERT_NE(extracted, nullptr);
	ASSERT_EQ(extracted->GetLength(), seq_payload.size());
	EXPECT_EQ(std::memcmp(extracted->GetDataAs<uint8_t>(), seq_payload.data(), seq_payload.size()), 0);
}

TEST(Av1ParserSequenceHeaderSummary, CapturesColorConfigFields)
{
	// AV1 spec 5.5.2 `color_config()`: with `seq_profile = 2` + `high_bitdepth = 1` + `twelve_bit = 1`
	// + 12-bit depth, the subsampling bits are explicitly read; `monochrome = 0`, `color_range = 0`,
	// `chroma_subsampling_x = 1`, `chroma_subsampling_y = 0` (4:2:2). When subsampling is not (1, 1),
	// `chroma_sample_position` is not present in the bitstream and the parser yields 0.
	SeqHeaderBuilder b;
	b.seq_profile				   = 2;
	b.operating_points_cnt_minus_1 = 0;
	b.operating_point_idc		   = {0};
	b.seq_level_idx				   = {10};
	b.seq_tier					   = {1};
	b.frame_width_bits_minus_1	   = 10;
	b.frame_height_bits_minus_1	   = 10;
	b.max_frame_width_minus_1	   = 1919;
	b.max_frame_height_minus_1	   = 1079;
	b.high_bitdepth				   = 1;
	b.twelve_bit				   = 1;
	b.monochrome				   = 0;
	b.color_range				   = 0;
	b.chroma_subsampling_x		   = 1;
	b.chroma_subsampling_y		   = 0;

	auto payload = BuildSequenceHeaderObuPayload(b);
	auto summary = Av1Parser::ParseSequenceHeaderSummary(payload.data(), payload.size());
	ASSERT_TRUE(summary.has_value());
	EXPECT_EQ(summary->seq_profile, 2);
	EXPECT_EQ(summary->seq_level_idx_0, 10);
	EXPECT_EQ(summary->seq_tier_0, 1);
	EXPECT_EQ(summary->high_bitdepth, 1);
	EXPECT_EQ(summary->twelve_bit, 1);
	EXPECT_EQ(summary->monochrome, 0);
	EXPECT_EQ(summary->chroma_subsampling_x, 1);
	EXPECT_EQ(summary->chroma_subsampling_y, 0);
	EXPECT_EQ(summary->chroma_sample_position, 0);
}

TEST(Av1ParserSequenceHeaderSummary, CapturesInitialDisplayDelayForOp0)
{
	// AV1 spec 5.5.1: when `initial_display_delay_present_flag == 1` and the per-op presence flag
	// for `op_0` is 1, a 4-bit `initial_display_delay_minus_1[0]` is read. The summary parser
	// records both values for `op_0` (diagnostics only - this Sequence Header field is NOT
	// cross-checked against the av1C `initial_presentation_delay`; they are distinct fields).
	SeqHeaderBuilder b;
	b.seq_profile							= 0;
	b.initial_display_delay_present_flag	= true;
	b.operating_points_cnt_minus_1			= 0;
	b.operating_point_idc					= {0};
	b.seq_level_idx							= {4};
	b.initial_display_delay_present_for_this_op = {1};
	b.initial_display_delay_minus_1			= {9};
	b.frame_width_bits_minus_1				= 10;
	b.frame_height_bits_minus_1				= 10;
	b.max_frame_width_minus_1				= 1919;
	b.max_frame_height_minus_1				= 1079;

	auto payload = BuildSequenceHeaderObuPayload(b);
	auto summary = Av1Parser::ParseSequenceHeaderSummary(payload.data(), payload.size());
	ASSERT_TRUE(summary.has_value());
	EXPECT_EQ(summary->initial_display_delay_present_for_op_0, 1);
	EXPECT_EQ(summary->initial_display_delay_minus_1_for_op_0, 9);
}

// frame OBU payload, uncompressed_header() prefix: show_existing_frame(1) | frame_type(2).
//   {0x00} -> show_existing=0, frame_type=0 (KEY_FRAME)
//   {0x20} -> show_existing=0, frame_type=1 (INTER_FRAME)
//   {0x80} -> show_existing_frame=1

TEST(Av1ParserIsKeyFrame, FrameObuKeyFrame)
{
	auto frame = MakeObu(Av1ObuType::Frame, {0x00});
	auto data  = std::make_shared<ov::Data>(frame.data(), frame.size());
	EXPECT_TRUE(Av1Parser::IsKeyFrame(data));
}

TEST(Av1ParserIsKeyFrame, FrameObuInterFrame)
{
	auto frame = MakeObu(Av1ObuType::Frame, {0x20});
	auto data  = std::make_shared<ov::Data>(frame.data(), frame.size());
	EXPECT_FALSE(Av1Parser::IsKeyFrame(data));
}

TEST(Av1ParserIsKeyFrame, ShowExistingFrameIsNotKey)
{
	auto frame = MakeObu(Av1ObuType::Frame, {0x80});
	auto data  = std::make_shared<ov::Data>(frame.data(), frame.size());
	EXPECT_FALSE(Av1Parser::IsKeyFrame(data));
}

TEST(Av1ParserIsKeyFrame, SequenceHeaderThenKeyFrame)
{
	SeqHeaderBuilder b;
	auto seq   = MakeObu(Av1ObuType::SequenceHeader, BuildSequenceHeaderObuPayload(b));
	auto frame = MakeObu(Av1ObuType::Frame, {0x00});

	std::vector<uint8_t> blob;
	blob.insert(blob.end(), seq.begin(), seq.end());
	blob.insert(blob.end(), frame.begin(), frame.end());

	auto data = std::make_shared<ov::Data>(blob.data(), blob.size());
	EXPECT_TRUE(Av1Parser::IsKeyFrame(data));
}

// reduced_still_picture_header => frame_type is always KEY_FRAME, even when the frame OBU bits
// would otherwise read as INTER.
TEST(Av1ParserIsKeyFrame, ReducedStillPictureIsAlwaysKey)
{
	SeqHeaderBuilder b;
	b.still_picture				   = true;
	b.reduced_still_picture_header = true;
	auto seq   = MakeObu(Av1ObuType::SequenceHeader, BuildSequenceHeaderObuPayload(b));
	auto frame = MakeObu(Av1ObuType::Frame, {0x20});

	std::vector<uint8_t> blob;
	blob.insert(blob.end(), seq.begin(), seq.end());
	blob.insert(blob.end(), frame.begin(), frame.end());

	auto data = std::make_shared<ov::Data>(blob.data(), blob.size());
	EXPECT_TRUE(Av1Parser::IsKeyFrame(data));
}

TEST(Av1ParserHasSequenceHeaderObu, PresentAndAbsent)
{
	auto seq   = MakeObu(Av1ObuType::SequenceHeader, {0x00});
	auto frame = MakeObu(Av1ObuType::Frame, {0x00});

	std::vector<uint8_t> with_seq;
	with_seq.insert(with_seq.end(), seq.begin(), seq.end());
	with_seq.insert(with_seq.end(), frame.begin(), frame.end());
	auto d1 = std::make_shared<ov::Data>(with_seq.data(), with_seq.size());
	EXPECT_TRUE(Av1Parser::HasSequenceHeaderObu(d1));

	auto d2 = std::make_shared<ov::Data>(frame.data(), frame.size());
	EXPECT_FALSE(Av1Parser::HasSequenceHeaderObu(d2));
}

TEST(Av1ParserExtractSequenceHeaderRaw, ReturnsFullObu)
{
	auto td  = MakeObu(Av1ObuType::TemporalDelimiter, {});
	auto seq = MakeObu(Av1ObuType::SequenceHeader, {0xAA, 0xBB});

	std::vector<uint8_t> blob;
	blob.insert(blob.end(), td.begin(), td.end());
	blob.insert(blob.end(), seq.begin(), seq.end());

	auto data = std::make_shared<ov::Data>(blob.data(), blob.size());
	auto raw  = Av1Parser::ExtractFirstSequenceHeaderObuRaw(data);
	ASSERT_NE(raw, nullptr);
	// The full OBU (header + obu_size + payload) equals the SequenceHeader OBU bytes.
	ASSERT_EQ(raw->GetLength(), seq.size());
	EXPECT_EQ(std::memcmp(raw->GetDataAs<uint8_t>(), seq.data(), seq.size()), 0);
}

TEST(Av1ParserExtractSequenceHeaderRaw, NullWhenAbsent)
{
	auto frame = MakeObu(Av1ObuType::Frame, {0x00});
	auto data  = std::make_shared<ov::Data>(frame.data(), frame.size());
	EXPECT_EQ(Av1Parser::ExtractFirstSequenceHeaderObuRaw(data), nullptr);
}

// A sequence header OBU without obu_has_size_field cannot be stored in configOBUs -> nullptr.
TEST(Av1ParserExtractSequenceHeaderRaw, NullWhenNotSizeDelimited)
{
	auto seq  = MakeObu(Av1ObuType::SequenceHeader, {0xAA}, /*has_size_field=*/false);
	auto data = std::make_shared<ov::Data>(seq.data(), seq.size());
	EXPECT_EQ(Av1Parser::ExtractFirstSequenceHeaderObuRaw(data), nullptr);
}

// A sequence header OBU with a zero-length payload is malformed -> treated as absent (nullptr),
// not a non-null empty buffer.
TEST(Av1ParserExtractSequenceHeader, ReturnsNullForEmptySequenceHeader)
{
	auto seq  = MakeObu(Av1ObuType::SequenceHeader, {});
	auto data = std::make_shared<ov::Data>(seq.data(), seq.size());
	EXPECT_EQ(Av1Parser::ExtractFirstSequenceHeaderObu(data), nullptr);
}
