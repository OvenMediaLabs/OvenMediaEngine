//==============================================================================
//
//  OvenMediaEngine - Unit Tests
//
//  Covers: NalUnitBitstreamParser emulation_prevention_three_byte handling
//
//==============================================================================
#include <gtest/gtest.h>
#include <modules/bitstream/nalu/nal_unit_bitstream_parser.h>

TEST(NalUnitBitstreamParser, SkipsAnEmulationPreventionByte)
{
	// 00 00 03 01: the 0x03 is an escape, so 0x01 is the byte that follows the two zeros
	const uint8_t nal[]			   = {0x67, 0x00, 0x00, 0x03, 0x01};
	const uint8_t unescaped[]	   = {0x67, 0x00, 0x00, 0x01};
	NalUnitBitstreamParser parser(nal, sizeof(nal));

	for (size_t i = 0; i < sizeof(unescaped); i++)
	{
		uint8_t value = 0;
		ASSERT_TRUE(parser.ReadU8(value)) << "byte: " << i;
		EXPECT_EQ(value, unescaped[i]) << "byte: " << i;
	}
}

TEST(NalUnitBitstreamParser, KeepsATrailingEmulationPreventionByte)
{
	// The same 00 00 03 with nothing behind it, so it cannot be an escape
	const uint8_t nal[] = {0x67, 0x00, 0x00, 0x03};
	NalUnitBitstreamParser parser(nal, sizeof(nal));

	for (size_t i = 0; i < sizeof(nal); i++)
	{
		uint8_t value = 0;
		ASSERT_TRUE(parser.ReadU8(value)) << "byte: " << i;
		EXPECT_EQ(value, nal[i]) << "byte: " << i;
	}
}
