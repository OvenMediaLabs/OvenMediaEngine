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
#include <modules/bitstream/av1/av1_decoder_configuration_record.h>
#include <modules/bitstream/av1/av1_types.h>

namespace
{
	std::shared_ptr<ov::Data> MakeData(const std::vector<uint8_t> &bytes)
	{
		return std::make_shared<ov::Data>(bytes.data(), bytes.size());
	}

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

	// Build a spec-correct OBU: 1-byte header + LEB128 size + payload.
	std::vector<uint8_t> BuildObu(Av1ObuType type, const std::vector<uint8_t> &payload)
	{
		std::vector<uint8_t> bytes;
		uint8_t header = 0;
		header |= (static_cast<uint8_t>(type) & 0x0F) << 3;
		// `extension_flag = 0`, `has_size_field = 1`, `reserved_1bit = 0`.
		header |= 1 << 1;
		bytes.push_back(header);
		auto leb = EncodeLeb128(payload.size());
		bytes.insert(bytes.end(), leb.begin(), leb.end());
		bytes.insert(bytes.end(), payload.begin(), payload.end());
		return bytes;
	}

	// Build a minimal valid `OBU_SEQUENCE_HEADER` payload with the given
	// `seq_profile` / `seq_level_idx_0` plus optional `color_config()` overrides.
	// `reduced_still_picture_header = 1` (with `still_picture = 1` per AV1 spec 5.5.1) keeps the
	// payload short while still serializing every field through `color_config()`.
	struct ReducedSeqHeaderFields
	{
		uint8_t seq_profile				= 0;
		uint8_t seq_level_idx_0			= 0;
		uint8_t high_bitdepth			= 0;
		uint8_t twelve_bit				= 0;
		uint8_t monochrome				= 0;
		uint8_t color_range				= 0;
		// AV1 spec defaults under seq_profile 0 (8-bit, color_desc=0) - subsampling 4:2:0.
		uint8_t chroma_subsampling_x	= 1;
		uint8_t chroma_subsampling_y	= 1;
		uint8_t chroma_sample_position	= 0;
	};

	std::vector<uint8_t> BuildReducedSeqHeaderPayloadEx(const ReducedSeqHeaderFields &f)
	{
		ov::BitWriter w(8);
		w.WriteBits(3, f.seq_profile);
		// AV1 spec 5.5.1: reduced_still_picture_header == 1 requires still_picture == 1.
		w.WriteBits(1, 1);   // still_picture
		w.WriteBits(1, 1);   // reduced_still_picture_header
		w.WriteBits(5, f.seq_level_idx_0);
		w.WriteBits(4, 0);   // frame_width_bits_minus_1 -> 1 bit
		w.WriteBits(4, 0);   // frame_height_bits_minus_1 -> 1 bit
		w.WriteBits(1, 0);   // max_frame_width_minus_1
		w.WriteBits(1, 0);   // max_frame_height_minus_1
		// AV1 spec 5.5.1 reduced branch continuation.
		w.WriteBits(1, 0);   // use_128x128_superblock
		w.WriteBits(1, 0);   // enable_filter_intra
		w.WriteBits(1, 0);   // enable_intra_edge_filter
		w.WriteBits(1, 0);   // enable_superres
		w.WriteBits(1, 0);   // enable_cdef
		w.WriteBits(1, 0);   // enable_restoration
		// AV1 spec 5.5.2 `color_config()`.
		w.WriteBits(1, f.high_bitdepth);
		if ((f.seq_profile == 2) && (f.high_bitdepth != 0))
		{
			w.WriteBits(1, f.twelve_bit);
		}
		if (f.seq_profile != 1)
		{
			w.WriteBits(1, f.monochrome);
		}
		w.WriteBits(1, 0);   // color_description_present_flag = 0 -> CP/TC/MC defaults
		if (f.monochrome != 0)
		{
			w.WriteBits(1, f.color_range);
		}
		else
		{
			w.WriteBits(1, f.color_range);
			// seq_profile 0 -> subsampling 1,1 implicit; seq_profile 1 -> 0,0 implicit.
			// seq_profile 2 with bit_depth != 12 -> 1,0 implicit; otherwise reads bits.
			if (f.seq_profile == 2)
			{
				const uint8_t bit_depth = (f.high_bitdepth != 0) ? ((f.twelve_bit != 0) ? 12 : 10) : 8;
				if (bit_depth == 12)
				{
					w.WriteBits(1, f.chroma_subsampling_x);
					if (f.chroma_subsampling_x != 0)
					{
						w.WriteBits(1, f.chroma_subsampling_y);
					}
				}
			}
			uint8_t eff_x = f.chroma_subsampling_x;
			uint8_t eff_y = f.chroma_subsampling_y;
			if (f.seq_profile == 0)
			{
				eff_x = 1;
				eff_y = 1;
			}
			else if (f.seq_profile == 1)
			{
				eff_x = 0;
				eff_y = 0;
			}
			else if (f.seq_profile == 2)
			{
				const uint8_t bit_depth = (f.high_bitdepth != 0) ? ((f.twelve_bit != 0) ? 12 : 10) : 8;
				if (bit_depth != 12)
				{
					eff_x = 1;
					eff_y = 0;
				}
			}
			if ((eff_x != 0) && (eff_y != 0))
			{
				w.WriteBits(2, f.chroma_sample_position);
			}
		}
		return std::vector<uint8_t>(w.GetData(), w.GetData() + w.GetDataSize());
	}

	std::vector<uint8_t> BuildReducedSeqHeaderPayload(uint8_t seq_profile, uint8_t seq_level_idx_0)
	{
		ReducedSeqHeaderFields f;
		f.seq_profile	  = seq_profile;
		f.seq_level_idx_0 = seq_level_idx_0;
		return BuildReducedSeqHeaderPayloadEx(f);
	}

	// Build a Sequence Header OBU payload that exercises the full (non-reduced) path with one
	// operating point. AV1 spec 5.5.1 requires `seq_tier[0]` to be read only when
	// `seq_level_idx[0] > 7`; this helper supports both branches via `seq_tier_0`.
	std::vector<uint8_t> BuildFullSeqHeaderPayload(uint8_t seq_profile, uint8_t seq_level_idx_0, uint8_t seq_tier_0)
	{
		ov::BitWriter w(16);
		w.WriteBits(3, seq_profile);
		w.WriteBits(1, 0);	 // still_picture
		w.WriteBits(1, 0);	 // reduced_still_picture_header
		w.WriteBits(1, 0);	 // timing_info_present_flag
		w.WriteBits(1, 0);	 // initial_display_delay_present_flag
		w.WriteBits(5, 0);	 // operating_points_cnt_minus_1
		w.WriteBits(12, 0);	 // operating_point_idc[0]
		w.WriteBits(5, seq_level_idx_0);
		if (seq_level_idx_0 > 7)
		{
			w.WriteBits(1, seq_tier_0);
		}
		w.WriteBits(4, 0);	 // frame_width_bits_minus_1 -> 1 bit
		w.WriteBits(4, 0);	 // frame_height_bits_minus_1 -> 1 bit
		w.WriteBits(1, 0);	 // max_frame_width_minus_1
		w.WriteBits(1, 0);	 // max_frame_height_minus_1
		// AV1 spec 5.5.1 non-reduced continuation.
		w.WriteBits(1, 0);	 // frame_id_numbers_present_flag
		w.WriteBits(1, 0);	 // use_128x128_superblock
		w.WriteBits(1, 0);	 // enable_filter_intra
		w.WriteBits(1, 0);	 // enable_intra_edge_filter
		w.WriteBits(1, 0);	 // enable_interintra_compound
		w.WriteBits(1, 0);	 // enable_masked_compound
		w.WriteBits(1, 0);	 // enable_warped_motion
		w.WriteBits(1, 0);	 // enable_dual_filter
		w.WriteBits(1, 0);	 // enable_order_hint
		w.WriteBits(1, 1);	 // seq_choose_screen_content_tools -> SELECT path
		w.WriteBits(1, 1);	 // seq_choose_integer_mv -> SELECT path (force=2 > 0)
		w.WriteBits(1, 0);	 // enable_superres
		w.WriteBits(1, 0);	 // enable_cdef
		w.WriteBits(1, 0);	 // enable_restoration
		// AV1 spec 5.5.2 `color_config()` for seq_profile 0: 8-bit, 4:2:0.
		w.WriteBits(1, 0);	 // high_bitdepth
		// seq_profile != 1 -> read monochrome.
		if (seq_profile != 1)
		{
			w.WriteBits(1, 0);	 // monochrome = 0
		}
		w.WriteBits(1, 0);	 // color_description_present_flag
		w.WriteBits(1, 0);	 // color_range
		// seq_profile 0 -> subsampling implicit (1, 1), chroma_sample_position read (2 bits).
		// seq_profile 1 -> subsampling implicit (0, 0), no chroma_sample_position.
		if (seq_profile == 0)
		{
			w.WriteBits(2, 0);	 // chroma_sample_position
		}
		return std::vector<uint8_t>(w.GetData(), w.GetData() + w.GetDataSize());
	}

	// Variant of `BuildFullSeqHeaderPayload` that exercises the
	// `initial_display_delay_present_flag` branch. AV1 spec 5.5.1: when the global flag is 1, each
	// operating point carries a 1-bit `initial_display_delay_present_for_this_op[i]`; when that bit
	// is 1, a 4-bit `initial_display_delay_minus_1[i]` follows.
	std::vector<uint8_t> BuildFullSeqHeaderPayloadWithInitialDisplayDelay(
		uint8_t seq_profile,
		uint8_t seq_level_idx_0,
		uint8_t seq_tier_0,
		uint8_t initial_display_delay_present_for_op_0,
		uint8_t initial_display_delay_minus_1_for_op_0)
	{
		ov::BitWriter w(16);
		w.WriteBits(3, seq_profile);
		w.WriteBits(1, 0);	 // still_picture
		w.WriteBits(1, 0);	 // reduced_still_picture_header
		w.WriteBits(1, 0);	 // timing_info_present_flag
		w.WriteBits(1, 1);	 // initial_display_delay_present_flag = 1 (enable per-op signaling)
		w.WriteBits(5, 0);	 // operating_points_cnt_minus_1
		w.WriteBits(12, 0);	 // operating_point_idc[0]
		w.WriteBits(5, seq_level_idx_0);
		if (seq_level_idx_0 > 7)
		{
			w.WriteBits(1, seq_tier_0);
		}
		// AV1 spec 5.5.1: per-op presence flag follows when global flag == 1.
		w.WriteBits(1, initial_display_delay_present_for_op_0);
		if (initial_display_delay_present_for_op_0 != 0)
		{
			w.WriteBits(4, initial_display_delay_minus_1_for_op_0 & 0x0F);
		}
		w.WriteBits(4, 0);	 // frame_width_bits_minus_1 -> 1 bit
		w.WriteBits(4, 0);	 // frame_height_bits_minus_1 -> 1 bit
		w.WriteBits(1, 0);	 // max_frame_width_minus_1
		w.WriteBits(1, 0);	 // max_frame_height_minus_1
		// AV1 spec 5.5.1 non-reduced continuation.
		w.WriteBits(1, 0);	 // frame_id_numbers_present_flag
		w.WriteBits(1, 0);	 // use_128x128_superblock
		w.WriteBits(1, 0);	 // enable_filter_intra
		w.WriteBits(1, 0);	 // enable_intra_edge_filter
		w.WriteBits(1, 0);	 // enable_interintra_compound
		w.WriteBits(1, 0);	 // enable_masked_compound
		w.WriteBits(1, 0);	 // enable_warped_motion
		w.WriteBits(1, 0);	 // enable_dual_filter
		w.WriteBits(1, 0);	 // enable_order_hint
		w.WriteBits(1, 1);	 // seq_choose_screen_content_tools -> SELECT path
		w.WriteBits(1, 1);	 // seq_choose_integer_mv -> SELECT path (force=2 > 0)
		w.WriteBits(1, 0);	 // enable_superres
		w.WriteBits(1, 0);	 // enable_cdef
		w.WriteBits(1, 0);	 // enable_restoration
		// AV1 spec 5.5.2 `color_config()` for seq_profile 0: 8-bit, 4:2:0.
		w.WriteBits(1, 0);	 // high_bitdepth
		if (seq_profile != 1)
		{
			w.WriteBits(1, 0);	 // monochrome = 0
		}
		w.WriteBits(1, 0);	 // color_description_present_flag
		w.WriteBits(1, 0);	 // color_range
		if (seq_profile == 0)
		{
			w.WriteBits(2, 0);	 // chroma_sample_position
		}
		return std::vector<uint8_t>(w.GetData(), w.GetData() + w.GetDataSize());
	}

	std::vector<uint8_t> BuildSeqHeaderObu(uint8_t seq_profile, uint8_t seq_level_idx_0)
	{
		return BuildObu(Av1ObuType::SequenceHeader, BuildReducedSeqHeaderPayload(seq_profile, seq_level_idx_0));
	}

	// Build a minimal valid `av1C` blob for the given fields, no `configOBUs`.
	//
	// `byte0`: `marker(1)=1`, `version(7)=1`                              -> `0x81`
	// `byte1`: `seq_profile(3)`, `seq_level_idx_0(5)`
	// `byte2`: `seq_tier_0(1)`, `high_bitdepth(1)`, `twelve_bit(1)`,
	//          `monochrome(1)`, `chroma_subsampling_x(1)`,
	//          `chroma_subsampling_y(1)`, `chroma_sample_position(2)`
	// `byte3`: `reserved(3)=0`, `initial_presentation_delay_present(1)`,
	//          [`delay_minus_one(4)` | `reserved(4)=0`]
	std::vector<uint8_t> BuildAv1cHeader(
		uint8_t seq_profile,
		uint8_t seq_level_idx_0,
		uint8_t seq_tier_0,
		uint8_t high_bitdepth,
		uint8_t twelve_bit,
		uint8_t monochrome,
		uint8_t chroma_subsampling_x,
		uint8_t chroma_subsampling_y,
		uint8_t chroma_sample_position,
		bool delay_present,
		uint8_t delay_minus_one)
	{
		std::vector<uint8_t> bytes;
		bytes.push_back(0x81);
		bytes.push_back(static_cast<uint8_t>(((seq_profile & 0x07) << 5) | (seq_level_idx_0 & 0x1F)));

		uint8_t b2 = 0;
		b2 |= (seq_tier_0 & 0x01) << 7;
		b2 |= (high_bitdepth & 0x01) << 6;
		b2 |= (twelve_bit & 0x01) << 5;
		b2 |= (monochrome & 0x01) << 4;
		b2 |= (chroma_subsampling_x & 0x01) << 3;
		b2 |= (chroma_subsampling_y & 0x01) << 2;
		b2 |= (chroma_sample_position & 0x03);
		bytes.push_back(b2);

		uint8_t b3 = 0;
		b3 |= (delay_present ? 0x10 : 0x00);
		b3 |= (delay_present ? (delay_minus_one & 0x0F) : 0x00);
		bytes.push_back(b3);

		return bytes;
	}
}  // namespace

TEST(AV1DecoderConfigurationRecord, ParseValidMainProfile8Bit)
{
	// `profile=0`, `level_idx=4` (=> `"04"`), `tier=0` (`M`), `high_bitdepth=0`,
	// `twelve_bit=0`, `monochrome=0`, `sub_x=1`, `sub_y=1`, `csp=0`,
	// no presentation delay.
	auto bytes = BuildAv1cHeader(0, 4, 0, 0, 0, 0, 1, 1, 0, false, 0);

	AV1DecoderConfigurationRecord record;
	ASSERT_TRUE(record.Parse(MakeData(bytes)));

	EXPECT_EQ(record.Marker(), 1);
	EXPECT_EQ(record.Version(), 1);
	EXPECT_EQ(record.SeqProfile(), 0);
	EXPECT_EQ(record.SeqLevelIdx0(), 4);
	EXPECT_EQ(record.SeqTier0(), 0);
	EXPECT_EQ(record.HighBitdepth(), 0);
	EXPECT_EQ(record.TwelveBit(), 0);
	EXPECT_EQ(record.Monochrome(), 0);
	EXPECT_EQ(record.ChromaSubsamplingX(), 1);
	EXPECT_EQ(record.ChromaSubsamplingY(), 1);
	EXPECT_EQ(record.ChromaSamplePosition(), 0);
	EXPECT_EQ(record.InitialPresentationDelayPresent(), 0);
	EXPECT_EQ(record.BitDepth(), 8);
	EXPECT_TRUE(record.IsValid());
	EXPECT_EQ(record.GetCodecsParameter(), "av01.0.04M.08");
}

TEST(AV1DecoderConfigurationRecord, ParseHighTier10Bit)
{
	// `profile=2`, `level_idx=8`, `tier=1` (`H`), `high_bitdepth=1`,
	// `twelve_bit=0` -> 10-bit
	auto bytes = BuildAv1cHeader(2, 8, 1, 1, 0, 0, 1, 1, 0, false, 0);

	AV1DecoderConfigurationRecord record;
	ASSERT_TRUE(record.Parse(MakeData(bytes)));

	EXPECT_EQ(record.SeqProfile(), 2);
	EXPECT_EQ(record.SeqLevelIdx0(), 8);
	EXPECT_EQ(record.SeqTier0(), 1);
	EXPECT_EQ(record.BitDepth(), 10);
	EXPECT_EQ(record.GetCodecsParameter(), "av01.2.08H.10");
}

TEST(AV1DecoderConfigurationRecord, ParseHighTier12Bit)
{
	// `profile=2`, `high_bitdepth=1`, `twelve_bit=1` -> 12-bit
	auto bytes = BuildAv1cHeader(2, 13, 1, 1, 1, 0, 1, 1, 0, false, 0);

	AV1DecoderConfigurationRecord record;
	ASSERT_TRUE(record.Parse(MakeData(bytes)));

	EXPECT_EQ(record.BitDepth(), 12);
	EXPECT_EQ(record.GetCodecsParameter(), "av01.2.13H.12");
}

TEST(AV1DecoderConfigurationRecord, ParseWithInitialPresentationDelay)
{
	auto bytes = BuildAv1cHeader(0, 4, 0, 0, 0, 0, 1, 1, 0, true, 7);

	AV1DecoderConfigurationRecord record;
	ASSERT_TRUE(record.Parse(MakeData(bytes)));

	EXPECT_EQ(record.InitialPresentationDelayPresent(), 1);
	EXPECT_EQ(record.InitialPresentationDelayMinusOne(), 7);
}

TEST(AV1DecoderConfigurationRecord, ParseWithConfigObus)
{
	auto bytes = BuildAv1cHeader(0, 4, 0, 0, 0, 0, 1, 1, 0, false, 0);
	// Spec-valid `configOBUs`: a Sequence Header OBU whose `seq_profile` / `seq_level_idx_0`
	// match the fixed `av1C` fields (cross-check rule).
	const auto obus = BuildSeqHeaderObu(0, 4);
	bytes.insert(bytes.end(), obus.begin(), obus.end());

	AV1DecoderConfigurationRecord record;
	ASSERT_TRUE(record.Parse(MakeData(bytes)));

	auto config_obus = record.ConfigObus();
	ASSERT_NE(config_obus, nullptr);
	ASSERT_EQ(config_obus->GetLength(), obus.size());
	EXPECT_EQ(std::memcmp(config_obus->GetDataAs<uint8_t>(), obus.data(), obus.size()), 0);
}

TEST(AV1DecoderConfigurationRecord, RoundTripSerialize)
{
	// av1C must encode values consistent with the embedded Sequence Header OBU:
	//   seq_profile = 0  -> AV1 spec 5.5.2: subsampling 4:2:0 implicit (1, 1)
	//   seq_level_idx_0 = 4 (<=7 -> seq_tier_0 inferred to 0)
	//   high_bitdepth = 0, twelve_bit = 0, monochrome = 0
	//   chroma_sample_position = 0 (CSP_UNKNOWN)
	// The av1C also carries `initial_presentation_delay` (1/5 here), which must round-trip through
	// serialize/reparse below. It is NOT cross-checked against the Sequence Header's
	// `initial_display_delay` (distinct fields, no match rule - see `ValidateConfigObus`); the full
	// SH builder is used only so the embedded OBU is well-formed.
	auto bytes = BuildAv1cHeader(0, 4, 0, 0, 0, 0, 1, 1, 0, true, 5);
	const auto sh = BuildFullSeqHeaderPayloadWithInitialDisplayDelay(
		/*seq_profile=*/0, /*seq_level_idx_0=*/4, /*seq_tier_0=*/0,
		/*initial_display_delay_present_for_op_0=*/1,
		/*initial_display_delay_minus_1_for_op_0=*/5);
	const auto obus = BuildObu(Av1ObuType::SequenceHeader, sh);
	bytes.insert(bytes.end(), obus.begin(), obus.end());

	AV1DecoderConfigurationRecord record;
	ASSERT_TRUE(record.Parse(MakeData(bytes)));

	auto serialized = record.GetData();
	ASSERT_NE(serialized, nullptr);
	ASSERT_EQ(serialized->GetLength(), bytes.size());
	EXPECT_EQ(std::memcmp(serialized->GetDataAs<uint8_t>(), bytes.data(), bytes.size()), 0);

	AV1DecoderConfigurationRecord reparsed;
	ASSERT_TRUE(reparsed.Parse(serialized));
	EXPECT_EQ(reparsed.SeqProfile(), 0);
	EXPECT_EQ(reparsed.SeqLevelIdx0(), 4);
	EXPECT_EQ(reparsed.SeqTier0(), 0);
	EXPECT_EQ(reparsed.InitialPresentationDelayPresent(), 1);
	EXPECT_EQ(reparsed.InitialPresentationDelayMinusOne(), 5);
}

TEST(AV1DecoderConfigurationRecord, RejectsInvalidMarker)
{
	auto bytes = BuildAv1cHeader(0, 4, 0, 0, 0, 0, 1, 1, 0, false, 0);
	// Clear marker bit -> `byte0` becomes `0x01` (`marker=0`, `version=1`).
	bytes[0] = 0x01;

	AV1DecoderConfigurationRecord record;
	EXPECT_FALSE(record.Parse(MakeData(bytes)));
	EXPECT_FALSE(record.IsValid());
}

TEST(AV1DecoderConfigurationRecord, RejectsInvalidVersion)
{
	auto bytes = BuildAv1cHeader(0, 4, 0, 0, 0, 0, 1, 1, 0, false, 0);
	// `marker=1`, `version=2` -> `0x82`.
	bytes[0] = 0x82;

	AV1DecoderConfigurationRecord record;
	EXPECT_FALSE(record.Parse(MakeData(bytes)));
	EXPECT_FALSE(record.IsValid());
}

TEST(AV1DecoderConfigurationRecord, RejectsTooShort)
{
	std::vector<uint8_t> bytes = {0x81, 0x00, 0x00};

	AV1DecoderConfigurationRecord record;
	EXPECT_FALSE(record.Parse(MakeData(bytes)));
}

TEST(AV1DecoderConfigurationRecord, RejectsNullData)
{
	AV1DecoderConfigurationRecord record;
	EXPECT_FALSE(record.Parse(nullptr));
}

// ffmpeg's `libaom-av1` muxer over enhanced-RTMP emits an empty
// `AV1CodecConfigurationRecord` blob in its first `SequenceStart` message. The
// strict ISO/BMFF spec requires at least a marker + version + base fields, so
// `Parse()` returns false on empty input - the caller (`flv::VideoParser`) must
// synthesize a defaults blob in that case (see `flv_video_parser.cpp`).
//
// This test guards the strict-parser behavior: a fully empty buffer must be
// rejected (`Parse` returns false), and the synthesized defaults buffer used
// by the lenient ffmpeg-compat branch must `Parse` successfully.
TEST(AV1DecoderConfigurationRecord, RejectsEmptyBlob)
{
	AV1DecoderConfigurationRecord record;
	EXPECT_FALSE(record.Parse(std::make_shared<ov::Data>()));
	EXPECT_FALSE(record.IsValid());
}

TEST(AV1DecoderConfigurationRecord, ParsesSynthesizedDefaults)
{
	// Matches the literal used in `flv_video_parser.cpp` `ParseAV1` lenient
	// branch: `marker=1`, `version=1`, all other fields zero.
	const std::vector<uint8_t> defaults = {0x81, 0x00, 0x00, 0x00};

	AV1DecoderConfigurationRecord record;
	ASSERT_TRUE(record.Parse(MakeData(defaults)));
	EXPECT_TRUE(record.IsValid());
	EXPECT_EQ(record.Marker(), 1);
	EXPECT_EQ(record.Version(), 1);
	EXPECT_EQ(record.ConfigObus(), nullptr);
}

TEST(AV1DecoderConfigurationRecord, ParsesFfmpegLibaomAv1Capture)
{
	// Exact 17-byte `av1C` blob captured from `ffmpeg n6.x -c:v libaom-av1`
	// pushing to `OvenMediaEngine` over enhanced-RTMP. This is the SECOND
	// `SequenceStart` payload that arrives ~1.8s after the empty placeholder;
	// it must parse cleanly.
	const std::vector<uint8_t> ffmpeg_av1c = {
		0x81,
		0x00,
		0x0C,
		0x00,
		0x0A, 0x0B, 0x00, 0x00, 0x00, 0x04, 0x3C, 0xFF, 0xBC, 0xDA, 0xF9, 0x00, 0x40,
	};

	AV1DecoderConfigurationRecord record;
	ASSERT_TRUE(record.Parse(MakeData(ffmpeg_av1c)));
	EXPECT_TRUE(record.IsValid());

	EXPECT_EQ(record.Marker(), 1);
	EXPECT_EQ(record.Version(), 1);
	EXPECT_EQ(record.SeqProfile(), 0);
	EXPECT_EQ(record.SeqLevelIdx0(), 0);
	EXPECT_EQ(record.SeqTier0(), 0);
	EXPECT_EQ(record.HighBitdepth(), 0);
	EXPECT_EQ(record.TwelveBit(), 0);
	EXPECT_EQ(record.Monochrome(), 0);
	EXPECT_EQ(record.ChromaSubsamplingX(), 1);
	EXPECT_EQ(record.ChromaSubsamplingY(), 1);
	EXPECT_EQ(record.ChromaSamplePosition(), 0);
	EXPECT_EQ(record.InitialPresentationDelayPresent(), 0);
	EXPECT_EQ(record.BitDepth(), 8);

	auto config_obus = record.ConfigObus();
	ASSERT_NE(config_obus, nullptr);
	EXPECT_EQ(config_obus->GetLength(), 13U);
}

// AV1 ISOBMFF binding v1.2.0 section 2.3 defines `AV1CodecConfigurationRecord` as the
// canonical serialized form of the av1C box. `Equals()` therefore promises byte-for-byte
// equality of that form. Each case below differs by exactly ONE field and asserts the
// expected `Equals()` result.
TEST(AV1DecoderConfigurationRecord, EqualsByByteIdenticalSerializedForm)
{
	// Baseline: a fully-populated av1C blob.
	auto baseline_bytes = BuildAv1cHeader(0, 4, 0, 0, 0, 0, 1, 1, 0, /*delay_present=*/true, /*delay_minus_one=*/3);

	auto baseline = std::make_shared<AV1DecoderConfigurationRecord>();
	ASSERT_TRUE(baseline->Parse(MakeData(baseline_bytes)));

	// Identical clone -> Equals true.
	{
		auto clone = std::make_shared<AV1DecoderConfigurationRecord>();
		ASSERT_TRUE(clone->Parse(MakeData(baseline_bytes)));
		EXPECT_TRUE(baseline->Equals(clone));
	}

	// Differ only in `seq_profile` -> Equals false (regression check - old contract caught
	// this too, but the new contract still must).
	{
		auto bytes	= BuildAv1cHeader(1, 4, 0, 0, 0, 0, 0, 0, 0, true, 3);
		auto record = std::make_shared<AV1DecoderConfigurationRecord>();
		ASSERT_TRUE(record->Parse(MakeData(bytes)));
		EXPECT_FALSE(baseline->Equals(record));
	}

	// Differ only in `monochrome` -> Equals false. Old weak contract returned TRUE here.
	// `monochrome = 1` with seq_profile != 1 is a spec-valid av1C fixed-header encoding when
	// no configOBUs are attached (the cross-check only runs against an embedded SH).
	{
		auto bytes	= BuildAv1cHeader(0, 4, 0, 0, 0, 1, 1, 1, 0, true, 3);
		auto record = std::make_shared<AV1DecoderConfigurationRecord>();
		ASSERT_TRUE(record->Parse(MakeData(bytes)));
		EXPECT_FALSE(baseline->Equals(record));
	}

	// Differ only in `chroma_subsampling_x` -> Equals false. Old weak contract returned TRUE.
	{
		auto bytes	= BuildAv1cHeader(0, 4, 0, 0, 0, 0, 0, 1, 0, true, 3);
		auto record = std::make_shared<AV1DecoderConfigurationRecord>();
		ASSERT_TRUE(record->Parse(MakeData(bytes)));
		EXPECT_FALSE(baseline->Equals(record));
	}

	// Differ only in `chroma_sample_position` -> Equals false. Old weak contract returned TRUE.
	{
		auto bytes	= BuildAv1cHeader(0, 4, 0, 0, 0, 0, 1, 1, 2, true, 3);
		auto record = std::make_shared<AV1DecoderConfigurationRecord>();
		ASSERT_TRUE(record->Parse(MakeData(bytes)));
		EXPECT_FALSE(baseline->Equals(record));
	}

	// Differ only in `initial_presentation_delay_minus_one` (delay_present stays 1, value
	// changes) -> Equals false. Old weak contract returned TRUE.
	{
		auto bytes	= BuildAv1cHeader(0, 4, 0, 0, 0, 0, 1, 1, 0, true, 7);
		auto record = std::make_shared<AV1DecoderConfigurationRecord>();
		ASSERT_TRUE(record->Parse(MakeData(bytes)));
		EXPECT_FALSE(baseline->Equals(record));
	}

	// Differ only in `configOBUs` payload -> Equals false. Old weak contract returned TRUE.
	// Cross-check: both records share the same fixed-header fields but one attaches a
	// spec-valid Sequence Header OBU while the other does not.
	{
		auto bytes = BuildAv1cHeader(0, 4, 0, 0, 0, 0, 1, 1, 0, true, 3);
		const auto sh = BuildFullSeqHeaderPayloadWithInitialDisplayDelay(
			/*seq_profile=*/0, /*seq_level_idx_0=*/4, /*seq_tier_0=*/0,
			/*initial_display_delay_present_for_op_0=*/1,
			/*initial_display_delay_minus_1_for_op_0=*/3);
		const auto obus = BuildObu(Av1ObuType::SequenceHeader, sh);
		bytes.insert(bytes.end(), obus.begin(), obus.end());
		auto record = std::make_shared<AV1DecoderConfigurationRecord>();
		ASSERT_TRUE(record->Parse(MakeData(bytes)));
		EXPECT_FALSE(baseline->Equals(record));
	}

	// `dynamic_pointer_cast<AV1DecoderConfigurationRecord>` failure -> Equals false. A stub
	// non-AV1 sibling exercises the runtime-type branch without pulling in H264/H265 deps.
	{
		class StubDecoderConfigurationRecord : public DecoderConfigurationRecord
		{
		public:
			bool Parse(const std::shared_ptr<const ov::Data> & /*data*/) override { return true; }
			bool IsValid() const override { return true; }
			bool Equals(const std::shared_ptr<DecoderConfigurationRecord> & /*other*/) override { return false; }
			ov::String GetCodecsParameter() const override { return ""; }

		protected:
			std::shared_ptr<const ov::Data> Serialize() override { return std::make_shared<ov::Data>(); }
		};

		auto stub = std::make_shared<StubDecoderConfigurationRecord>();
		EXPECT_FALSE(baseline->Equals(stub));
	}

	// Null other -> Equals false (matches base-class contract).
	{
		EXPECT_FALSE(baseline->Equals(nullptr));
	}
}

TEST(AV1DecoderConfigurationRecord, RejectsMalformedConfigObu)
{
	// `configOBUs` containing an OBU with `obu_reserved_1bit = 1` -> rejected at the
	// OBU header walk per AV1 spec 5.3.2.
	auto bytes = BuildAv1cHeader(0, 4, 0, 0, 0, 0, 1, 1, 0, false, 0);
	bytes.push_back(0x09);  // type=SequenceHeader, ext=0, has_size=0, reserved=1 (invalid)
	bytes.push_back(0x00);

	AV1DecoderConfigurationRecord record;
	EXPECT_FALSE(record.Parse(MakeData(bytes)));
}

TEST(AV1DecoderConfigurationRecord, RejectsConfigObuSeqProfileMismatch)
{
	// AV1 ISOBMFF binding v1.2.0: Sequence Header OBU `seq_profile` must equal `av1C` field.
	auto bytes = BuildAv1cHeader(0, 4, 0, 0, 0, 0, 1, 1, 0, false, 0);
	// Embedded sequence header says `seq_profile=1`, but `av1C` says `0`.
	const auto obus = BuildSeqHeaderObu(1, 4);
	bytes.insert(bytes.end(), obus.begin(), obus.end());

	AV1DecoderConfigurationRecord record;
	EXPECT_FALSE(record.Parse(MakeData(bytes)));
}

TEST(AV1DecoderConfigurationRecord, RejectsConfigObuSeqLevelMismatch)
{
	// AV1 ISOBMFF binding v1.2.0: `seq_level_idx_0` cross-check failure.
	auto bytes = BuildAv1cHeader(0, 4, 0, 0, 0, 0, 1, 1, 0, false, 0);
	// Embedded sequence header says `seq_level_idx_0=7`, but `av1C` says `4`.
	const auto obus = BuildSeqHeaderObu(0, 7);
	bytes.insert(bytes.end(), obus.begin(), obus.end());

	AV1DecoderConfigurationRecord record;
	EXPECT_FALSE(record.Parse(MakeData(bytes)));
}

TEST(AV1DecoderConfigurationRecord, RejectsSequenceHeaderNotFirstObu)
{
	// AV1 ISOBMFF binding v1.2.0: "If present, the Sequence Header OBU SHALL be the first OBU."
	auto bytes	   = BuildAv1cHeader(0, 4, 0, 0, 0, 0, 1, 1, 0, false, 0);
	const auto td  = BuildObu(Av1ObuType::TemporalDelimiter, {});
	const auto seq = BuildSeqHeaderObu(0, 4);
	bytes.insert(bytes.end(), td.begin(), td.end());
	bytes.insert(bytes.end(), seq.begin(), seq.end());

	AV1DecoderConfigurationRecord record;
	EXPECT_FALSE(record.Parse(MakeData(bytes)));
}

TEST(AV1DecoderConfigurationRecord, RejectsMultipleSequenceHeaders)
{
	// AV1 ISOBMFF binding v1.2.0: "At most one Sequence Header OBU SHALL be present."
	// (Two sequence headers also violates the "first OBU" rule; either condition rejects.)
	auto bytes		 = BuildAv1cHeader(0, 4, 0, 0, 0, 0, 1, 1, 0, false, 0);
	const auto seq_a = BuildSeqHeaderObu(0, 4);
	const auto seq_b = BuildSeqHeaderObu(0, 4);
	bytes.insert(bytes.end(), seq_a.begin(), seq_a.end());
	bytes.insert(bytes.end(), seq_b.begin(), seq_b.end());

	AV1DecoderConfigurationRecord record;
	EXPECT_FALSE(record.Parse(MakeData(bytes)));
}

TEST(AV1DecoderConfigurationRecord, AcceptsConfigObusWithoutSequenceHeader)
{
	// `configOBUs` containing only a Temporal Delimiter is valid (no cross-check needed).
	auto bytes	  = BuildAv1cHeader(0, 4, 0, 0, 0, 0, 1, 1, 0, false, 0);
	const auto td = BuildObu(Av1ObuType::TemporalDelimiter, {});
	bytes.insert(bytes.end(), td.begin(), td.end());

	AV1DecoderConfigurationRecord record;
	EXPECT_TRUE(record.Parse(MakeData(bytes)));
}

TEST(AV1DecoderConfigurationRecord, RejectsSeqProfileReserved)
{
	// AV1 spec 6.4.1: "seq_profile shall be in the range of 0 to 2." Values 3..7 are reserved.
	for (uint8_t profile : {3, 4, 5, 6, 7})
	{
		// `BuildAv1cHeader` packs `seq_profile` as the top 3 bits of byte 1; values <= 7 fit.
		auto bytes = BuildAv1cHeader(profile, 4, 0, 0, 0, 0, 1, 1, 0, false, 0);
		AV1DecoderConfigurationRecord record;
		EXPECT_FALSE(record.Parse(MakeData(bytes))) << "seq_profile=" << static_cast<int>(profile);
	}
}

TEST(AV1DecoderConfigurationRecord, RejectsReservedChromaSamplePosition)
{
	// AV1 spec 6.4.2 Table 8: `chroma_sample_position = 3` (`CSP_RESERVED`) is reserved.
	auto bytes = BuildAv1cHeader(0, 4, 0, 0, 0, 0, 1, 1, 3, false, 0);
	AV1DecoderConfigurationRecord record;
	EXPECT_FALSE(record.Parse(MakeData(bytes)));
}

TEST(AV1DecoderConfigurationRecord, RejectsConfigObuSeqTierMismatch)
{
	// AV1 ISOBMFF binding v1.2.0 section 2.3.2: `seq_tier_0` cross-check failure. Embedded SH has
	// `seq_level_idx_0 = 8 > 7` and `seq_tier_0 = 1`, but av1C says `seq_tier_0 = 0`.
	auto bytes	  = BuildAv1cHeader(0, 8, 0, 0, 0, 0, 1, 1, 0, false, 0);
	const auto sh = BuildFullSeqHeaderPayload(0, 8, /*seq_tier_0=*/1);
	const auto obus = BuildObu(Av1ObuType::SequenceHeader, sh);
	bytes.insert(bytes.end(), obus.begin(), obus.end());

	AV1DecoderConfigurationRecord record;
	EXPECT_FALSE(record.Parse(MakeData(bytes)));
}

TEST(AV1DecoderConfigurationRecord, RejectsConfigObuHighBitDepthMismatch)
{
	// AV1 ISOBMFF binding v1.2.0 section 2.3.2: `high_bitdepth` cross-check failure. Embedded SH
	// sets `high_bitdepth = 1`; av1C declares 0.
	auto bytes = BuildAv1cHeader(0, 4, 0, 0, 0, 0, 1, 1, 0, false, 0);
	ReducedSeqHeaderFields f;
	f.seq_profile	  = 0;
	f.seq_level_idx_0 = 4;
	f.high_bitdepth	  = 1;	// mismatch
	const auto obus	  = BuildObu(Av1ObuType::SequenceHeader, BuildReducedSeqHeaderPayloadEx(f));
	bytes.insert(bytes.end(), obus.begin(), obus.end());

	AV1DecoderConfigurationRecord record;
	EXPECT_FALSE(record.Parse(MakeData(bytes)));
}

TEST(AV1DecoderConfigurationRecord, RejectsConfigObuMonochromeMismatch)
{
	// AV1 ISOBMFF binding v1.2.0 section 2.3.2: `monochrome` cross-check failure. Embedded SH sets
	// `monochrome = 1`; av1C declares 0. Monochrome forces subsampling (1, 1) implicitly so the
	// other av1C fields stay consistent.
	auto bytes = BuildAv1cHeader(0, 4, 0, 0, 0, 0, 1, 1, 0, false, 0);
	ReducedSeqHeaderFields f;
	f.seq_profile	  = 0;
	f.seq_level_idx_0 = 4;
	f.monochrome	  = 1;	// mismatch
	const auto obus	  = BuildObu(Av1ObuType::SequenceHeader, BuildReducedSeqHeaderPayloadEx(f));
	bytes.insert(bytes.end(), obus.begin(), obus.end());

	AV1DecoderConfigurationRecord record;
	EXPECT_FALSE(record.Parse(MakeData(bytes)));
}

TEST(AV1DecoderConfigurationRecord, RejectsConfigObuChromaSubsamplingMismatch)
{
	// AV1 ISOBMFF binding v1.2.0 section 2.3.2: `chroma_subsampling_x` / `chroma_subsampling_y` must
	// match. With `seq_profile = 1` the spec forces 4:4:4 (0, 0); but av1C declares (1, 1). The
	// embedded SH (profile 1) yields (0, 0) -> cross-check rejects.
	auto bytes = BuildAv1cHeader(1, 4, 0, 0, 0, 0, 1, 1, 0, false, 0);
	ReducedSeqHeaderFields f;
	f.seq_profile	  = 1;
	f.seq_level_idx_0 = 4;
	const auto obus	  = BuildObu(Av1ObuType::SequenceHeader, BuildReducedSeqHeaderPayloadEx(f));
	bytes.insert(bytes.end(), obus.begin(), obus.end());

	AV1DecoderConfigurationRecord record;
	EXPECT_FALSE(record.Parse(MakeData(bytes)));
}

TEST(AV1DecoderConfigurationRecord, RejectsConfigObuChromaSamplePositionMismatch)
{
	// AV1 ISOBMFF binding v1.2.0 section 2.3.2: "chroma_sample_position (when not zero) ... shall
	// match." av1C declares `chroma_sample_position = 2`; SH yields 0 (default in our builder when
	// not explicitly set) - mismatch must be rejected because av1C value is non-zero.
	auto bytes = BuildAv1cHeader(0, 4, 0, 0, 0, 0, 1, 1, 2, false, 0);
	ReducedSeqHeaderFields f;
	f.seq_profile			 = 0;
	f.seq_level_idx_0		 = 4;
	f.chroma_sample_position = 0;	 // mismatch vs av1C's 2
	const auto obus			 = BuildObu(Av1ObuType::SequenceHeader, BuildReducedSeqHeaderPayloadEx(f));
	bytes.insert(bytes.end(), obus.begin(), obus.end());

	AV1DecoderConfigurationRecord record;
	EXPECT_FALSE(record.Parse(MakeData(bytes)));
}

TEST(AV1DecoderConfigurationRecord, AcceptsZeroChromaSamplePositionMismatch)
{
	// AV1 ISOBMFF binding v1.2.0 section 2.3.2 spec exception: when av1C `chroma_sample_position`
	// is 0, the cross-check is skipped even if the embedded SH carries a non-zero value.
	auto bytes = BuildAv1cHeader(0, 4, 0, 0, 0, 0, 1, 1, 0, false, 0);
	ReducedSeqHeaderFields f;
	f.seq_profile			 = 0;
	f.seq_level_idx_0		 = 4;
	f.chroma_sample_position = 2;	 // SH says 2, but av1C says 0 - spec allows.
	const auto obus			 = BuildObu(Av1ObuType::SequenceHeader, BuildReducedSeqHeaderPayloadEx(f));
	bytes.insert(bytes.end(), obus.begin(), obus.end());

	AV1DecoderConfigurationRecord record;
	EXPECT_TRUE(record.Parse(MakeData(bytes)));
}

TEST(AV1DecoderConfigurationRecord, RejectsConfigObuWithoutSizeField)
{
	// AV1 ISOBMFF binding v1.2.0: every OBU inside `configOBUs` SHALL set `obu_has_size_field = 1`.
	// Build an av1C blob whose `configOBUs` contains a Sequence Header OBU with
	// `obu_has_size_field = 0` (header byte = `0x08`: `forbidden_bit=0`, `obu_type=1`,
	// `ext_flag=0`, `has_size=0`, `reserved_1bit=0`) -> strict parser must reject.
	auto bytes = BuildAv1cHeader(0, 4, 0, 0, 0, 0, 1, 1, 0, /*delay_present=*/false, /*delay_minus_one=*/0);
	// Manually craft the OBU since `BuildObu` always sets `has_size_field = 1`.
	const auto sh_payload = BuildReducedSeqHeaderPayload(0, 4);
	bytes.push_back(static_cast<uint8_t>(0x08));  // SH header, has_size_field = 0
	bytes.insert(bytes.end(), sh_payload.begin(), sh_payload.end());

	AV1DecoderConfigurationRecord record;
	EXPECT_FALSE(record.Parse(MakeData(bytes)));
}

TEST(AV1DecoderConfigurationRecord, RejectsFixedHeaderReserved3BitsNonZero)
{
	// AV1 ISOBMFF binding v1.2.0 section 2.3.1: the 3-bit `reserved` field in the fixed av1C
	// header SHALL be 0. `BuildAv1cHeader` always writes 0; inject a non-zero value into the top
	// 3 bits of byte 3 to exercise the strict reject.
	auto bytes = BuildAv1cHeader(0, 4, 0, 0, 0, 0, 1, 1, 0, /*delay_present=*/false, /*delay_minus_one=*/0);
	ASSERT_EQ(bytes.size(), 4U);
	// byte 3 layout: `reserved(3) | initial_presentation_delay_present(1) | [delay_minus_one(4) | reserved(4)]`.
	// Set top 3 bits to 0b101 -> 0xA0.
	bytes[3] = static_cast<uint8_t>((bytes[3] & 0x1F) | 0xA0);

	AV1DecoderConfigurationRecord record;
	EXPECT_FALSE(record.Parse(MakeData(bytes)));
	EXPECT_FALSE(record.IsValid());
}

TEST(AV1DecoderConfigurationRecord, RejectsFixedHeaderReserved4BitsNonZero)
{
	// AV1 ISOBMFF binding v1.2.0 section 2.3.1: when `initial_presentation_delay_present == 0`,
	// the trailing 4-bit `reserved` field SHALL be 0. `BuildAv1cHeader` writes 0; inject a
	// non-zero value into the low 4 bits of byte 3.
	auto bytes = BuildAv1cHeader(0, 4, 0, 0, 0, 0, 1, 1, 0, /*delay_present=*/false, /*delay_minus_one=*/0);
	ASSERT_EQ(bytes.size(), 4U);
	// Keep top 3 reserved bits at 0, presence bit at 0, set low 4 bits to 0xB.
	bytes[3] = static_cast<uint8_t>((bytes[3] & 0xF0) | 0x0B);

	AV1DecoderConfigurationRecord record;
	EXPECT_FALSE(record.Parse(MakeData(bytes)));
	EXPECT_FALSE(record.IsValid());
}
