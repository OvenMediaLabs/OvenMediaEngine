//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2026 OvenMediaLabs. All rights reserved.
//
//==============================================================================

//  Covers: H264Parser VUI timing_info handling, in particular the frame rate
//          division when num_units_in_tick sits at the edge of u(32).
//
//  Bitstreams are hand-built with a BitWriter so the field values under test
//  are known by construction. Syntax follows ITU-T H.264 (7.3.2.1.1 SPS,
//  E.1.1 vui_parameters).

// Unit tests
// ----------
// cmake -S . -B __temp_build/tests -G Ninja -DOME_BUILD_TESTS=ON
// ninja -C __temp_build/tests ome_test_modules
// ./__temp_build/tests/bin/ome_test_modules --gtest_filter='H264Parser.*'

#include <base/ovlibrary/bit_writer.h>
#include <gtest/gtest.h>
#include <modules/bitstream/h264/h264_parser.h>

#include <vector>

namespace
{
	// ue(v) (ITU-T H.264 9.1)
	void WriteUE(ov::BitWriter &w, uint32_t value)
	{
		uint64_t code_num = static_cast<uint64_t>(value) + 1;
		int numbits		  = 0;
		while ((code_num >> (numbits + 1)) != 0)
		{
			numbits++;
		}

		if (numbits > 0)
		{
			w.WriteBits(numbits, 0);
		}
		w.WriteBits(numbits + 1, code_num);
	}

	// rbsp_trailing_bits(): stop bit '1' then zero-pad to a byte boundary
	void WriteTrailing(ov::BitWriter &w)
	{
		w.WriteBits(1, 1);
		while ((w.GetBitCount() % 8) != 0)
		{
			w.WriteBits(1, 0);
		}
	}

	std::vector<uint8_t> ApplyEmulationPrevention(const std::vector<uint8_t> &rbsp)
	{
		std::vector<uint8_t> out;
		out.reserve(rbsp.size() + 4);

		size_t zeros = 0;
		for (uint8_t byte : rbsp)
		{
			if ((zeros >= 2) && (byte <= 0x03))
			{
				out.push_back(0x03);
				zeros = 0;
			}

			out.push_back(byte);
			zeros = (byte == 0x00) ? (zeros + 1) : 0;
		}

		return out;
	}

	// A 640x320 Baseline SPS whose VUI carries the given timing_info
	std::vector<uint8_t> MakeSpsNal(uint32_t num_units_in_tick, uint32_t time_scale,
									uint8_t fixed_frame_rate_flag = 1)
	{
		ov::BitWriter w(64);

		w.WriteBits(8, 66);	 // profile_idc: Baseline, so no chroma_format_idc block
		w.WriteBits(8, 0);	 // constraint flags + reserved
		w.WriteBits(8, 31);	 // level_idc
		WriteUE(w, 0);		 // seq_parameter_set_id
		WriteUE(w, 4);		 // log2_max_frame_num_minus4
		WriteUE(w, 0);		 // pic_order_cnt_type
		WriteUE(w, 4);		 // log2_max_pic_order_cnt_lsb_minus4
		WriteUE(w, 1);		 // max_num_ref_frames
		w.WriteBits(1, 0);	 // gaps_in_frame_num_value_allowed_flag
		WriteUE(w, 39);		 // pic_width_in_mbs_minus1
		WriteUE(w, 19);		 // pic_height_in_map_units_minus1
		w.WriteBits(1, 1);	 // frame_mbs_only_flag
		w.WriteBits(1, 1);	 // direct_8x8_inference_flag
		w.WriteBits(1, 0);	 // frame_cropping_flag

		w.WriteBits(1, 1);	// vui_parameters_present_flag
		w.WriteBits(1, 0);	// aspect_ratio_info_present_flag
		w.WriteBits(1, 0);	// overscan_info_present_flag
		w.WriteBits(1, 0);	// video_signal_type_present_flag
		w.WriteBits(1, 0);	// chroma_loc_info_present_flag

		w.WriteBits(1, 1);						// timing_info_present_flag
		w.WriteBits(32, num_units_in_tick);		// num_units_in_tick, u(32)
		w.WriteBits(32, time_scale);			// time_scale, u(32)
		w.WriteBits(1, fixed_frame_rate_flag);	// fixed_frame_rate_flag

		w.WriteBits(1, 0);	// nal_hrd_parameters_present_flag
		w.WriteBits(1, 0);	// vcl_hrd_parameters_present_flag
		w.WriteBits(1, 0);	// pic_struct_present_flag
		w.WriteBits(1, 0);	// bitstream_restriction_flag

		WriteTrailing(w);

		const std::vector<uint8_t> rbsp(w.GetData(), w.GetData() + w.GetDataSize());

		std::vector<uint8_t> nal;
		nal.push_back(0x67);  // forbidden_zero_bit(0) + nal_ref_idc(3) + nal_unit_type(7, SPS)

		const auto ebsp = ApplyEmulationPrevention(rbsp);
		nal.insert(nal.end(), ebsp.begin(), ebsp.end());

		return nal;
	}
}  // namespace

TEST(H264Parser, DerivesTheFrameRateFromTimingInfo)
{
	const auto nal = MakeSpsNal(1, 60);

	H264SPS sps;
	ASSERT_TRUE(H264Parser::ParseSPS(nal.data(), nal.size(), sps));

	EXPECT_EQ(sps.GetWidth(), 640U);
	EXPECT_EQ(sps.GetFps(), 30U);
}

// Doubling 0x80000000 in 32-bit arithmetic wraps the divisor to 0, which used to raise SIGFPE and
// terminate the process. The value is legal u(32) syntax, so the parser has to survive it.
TEST(H264Parser, SurvivesANumUnitsInTickThatWrapsTheDoubledDivisor)
{
	for (uint32_t time_scale : {0U, 1U, 53U, 60U, 0xFFFFFFFFU})
	{
		const auto nal = MakeSpsNal(0x80000000U, time_scale);

		H264SPS sps;
		ASSERT_TRUE(H264Parser::ParseSPS(nal.data(), nal.size(), sps))
			<< "time_scale: " << time_scale;

		// Any u(32) time_scale over a divisor of 2^32 truncates to 0
		EXPECT_EQ(sps.GetFps(), 0U) << "time_scale: " << time_scale;
	}
}

TEST(H264Parser, ReportsZeroFpsWhenNumUnitsInTickIsZero)
{
	const auto nal = MakeSpsNal(0, 60);

	H264SPS sps;
	ASSERT_TRUE(H264Parser::ParseSPS(nal.data(), nal.size(), sps));

	EXPECT_EQ(sps.GetFps(), 0U);
}

// ITU-T H.264 E.2.1 defines the clock tick without reference to `fixed_frame_rate_flag`,
// so an encoder that will not promise evenly spaced output times still carries a derivable frame rate
TEST(H264Parser, DerivesTheFrameRateWithoutFixedFrameRateFlag)
{
	const auto nal = MakeSpsNal(1001, 60000, 0);

	H264SPS sps;
	ASSERT_TRUE(H264Parser::ParseSPS(nal.data(), nal.size(), sps));

	EXPECT_EQ(sps.GetFps(), 29U);
}
