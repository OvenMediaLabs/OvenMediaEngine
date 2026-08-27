//==============================================================================
//
//  OvenMediaEngine - Unit Tests
//
//  Covers: H264SpsRewriter::EnablePicStructPresent against a real SPS that
//  carries an emulation_prevention_three_byte
//
//==============================================================================
#include <gtest/gtest.h>
#include <modules/bitstream/h264/h264_parser.h>
#include <modules/bitstream/h264/h264_sps_rewriter.h>
#include <modules/bitstream/nalu/nal_unit_insertor.h>

namespace
{
	// A 1920x1080 60 fps SPS as NVENC emitted it, taken off a live stream. Bytes 12..15 are
	// 00 00 03 03: the RBSP holds a literal 00 00 03 and the encoder escaped it. Unescaping before
	// handing this to ParseSPS makes the parser strip that 0x03 a second time and report a bit
	// position that belongs to no field.
	const std::vector<uint8_t> kSpsNal = {
		0x67, 0x42, 0xC0, 0x2A, 0x95, 0xA0, 0x1E, 0x00, 0x89, 0xF9, 0x70, 0x11,
		0x00, 0x00, 0x03, 0x03, 0xE8, 0x00, 0x01, 0xD4, 0xC0, 0xE0, 0x00, 0x00,
		0x13, 0x12, 0xD0, 0x00, 0x04, 0xC4, 0xB5, 0xBB, 0xCB, 0x82, 0x80};

	std::shared_ptr<ov::Data> SpsNal()
	{
		return std::make_shared<ov::Data>(kSpsNal.data(), kSpsNal.size());
	}

	// The same SPS with pic_struct_present_flag cleared, which is how an encoder that does not set
	// it would emit the parameter set.
	std::shared_ptr<ov::Data> SpsNalWithoutPicStruct()
	{
		auto nal = SpsNal();

		H264SPS sps;
		if (H264Parser::ParseSPS(nal->GetDataAs<uint8_t>(), nal->GetLength(), sps) == false)
		{
			return nullptr;
		}

		const size_t bit_pos = sps.GetPicStructPresentFlagBitPos();
		if (bit_pos == 0)
		{
			return nullptr;
		}

		auto cleared = nal->Clone();
		auto *buffer = cleared->GetWritableDataAs<uint8_t>();
		buffer[bit_pos / 8] &= static_cast<uint8_t>(~(0x80 >> (bit_pos % 8)));

		return cleared;
	}
}  // namespace

TEST(H264SpsRewriter, ParsesAnEscapedSpsCorrectly)
{
	auto nal = SpsNal();

	H264SPS sps;
	ASSERT_TRUE(H264Parser::ParseSPS(nal->GetDataAs<uint8_t>(), nal->GetLength(), sps));

	EXPECT_EQ(sps.GetWidth(), 1920U);
	EXPECT_EQ(sps.GetHeight(), 1080U);
	EXPECT_TRUE(sps.IsFrameMbsOnly());
	EXPECT_NE(sps.GetPicStructPresentFlagBitPos(), 0U) << "the SPS has a VUI, so the flag is coded";
}

TEST(H264SpsRewriter, ReturnsNullWhenTheFlagIsAlreadySet)
{
	auto nal = SpsNal();

	H264SPS sps;
	ASSERT_TRUE(H264Parser::ParseSPS(nal->GetDataAs<uint8_t>(), nal->GetLength(), sps));
	ASSERT_TRUE(sps.IsPicStructPresent()) << "this fixture was captured after the flag was set";

	EXPECT_EQ(H264SpsRewriter::EnablePicStructPresent(nal), nullptr);
}

TEST(H264SpsRewriter, SetsTheFlagWithoutDisturbingTheRestOfTheSps)
{
	auto unpatched = SpsNalWithoutPicStruct();
	ASSERT_NE(unpatched, nullptr);

	H264SPS before;
	ASSERT_TRUE(H264Parser::ParseSPS(unpatched->GetDataAs<uint8_t>(), unpatched->GetLength(), before));
	ASSERT_FALSE(before.IsPicStructPresent());

	// The result is an RBSP, because NalUnitInsertor escapes whatever NAL it is given
	auto patched_rbsp = H264SpsRewriter::EnablePicStructPresent(unpatched);
	ASSERT_NE(patched_rbsp, nullptr);

	// Put it back on the wire the way the insertor would, then read it as a decoder would
	auto patched_nal = NalUnitInsertor::EmulationPreventionBytes(patched_rbsp);
	ASSERT_NE(patched_nal, nullptr);

	H264SPS after;
	ASSERT_TRUE(H264Parser::ParseSPS(patched_nal->GetDataAs<uint8_t>(), patched_nal->GetLength(), after));

	EXPECT_TRUE(after.IsPicStructPresent());
	EXPECT_EQ(after.GetWidth(), before.GetWidth());
	EXPECT_EQ(after.GetHeight(), before.GetHeight());
	EXPECT_EQ(after.GetId(), before.GetId());
	EXPECT_EQ(after.GetProfileIdc(), before.GetProfileIdc());
	EXPECT_EQ(after.GetCodecLevelIdc(), before.GetCodecLevelIdc());
	EXPECT_EQ(after.GetFps(), before.GetFps())
		<< "num_units_in_tick and time_scale sit before the flag and must be untouched";
	EXPECT_EQ(after.GetPicStructPresentFlagBitPos(), before.GetPicStructPresentFlagBitPos());

	// Escaping is not part of the contract, but the round trip must restore the original NAL
	EXPECT_EQ(patched_nal->GetLength(), unpatched->GetLength());
}

TEST(H264SpsRewriter, RejectsInputThatIsNotAnSps)
{
	EXPECT_EQ(H264SpsRewriter::EnablePicStructPresent(nullptr), nullptr);

	// Too short to hold a NAL header and a payload
	const uint8_t one_byte[] = {0x67};
	EXPECT_EQ(H264SpsRewriter::EnablePicStructPresent(std::make_shared<ov::Data>(one_byte, sizeof(one_byte))), nullptr);

	// A PPS, not an SPS
	const uint8_t pps[] = {0x68, 0xCE, 0x3C, 0x80};
	EXPECT_EQ(H264SpsRewriter::EnablePicStructPresent(std::make_shared<ov::Data>(pps, sizeof(pps))), nullptr);
}
