//==============================================================================
//
//  OvenMediaEngine - Unit Tests
//
//  Covers: NalUnitBitstreamParser emulation_prevention_three_byte handling and the
//          Exp-Golomb range accepted by ReadUEV()
//
//==============================================================================
#include <gtest/gtest.h>
#include <modules/bitstream/nalu/nal_unit_bitstream_parser.h>

#include <vector>

namespace
{
	// Builds a bitstream holding one Exp-Golomb code: leading_zero_bits zero bits, the terminating
	// 1 bit, then leading_zero_bits bits of rest. Trailing zero bytes keep the reads in bounds.
	std::vector<uint8_t> MakeUevBitstream(int leading_zero_bits, uint64_t rest)
	{
		std::vector<uint8_t> bytes;
		size_t bit_count = 0;

		auto write_bit	 = [&](int bit) {
			if ((bit_count % 8) == 0)
			{
				bytes.push_back(0x00);
			}

			if (bit != 0)
			{
				bytes.back() |= static_cast<uint8_t>(0x80 >> (bit_count % 8));
			}

			bit_count++;
		};

		for (int i = 0; i < leading_zero_bits; i++)
		{
			write_bit(0);
		}

		write_bit(1);

		for (int i = leading_zero_bits - 1; i >= 0; i--)
		{
			// rest carries the low 64 bits, and shifting a uint64_t by 64 or more is undefined.
			// Every position above that is zero.
			write_bit((i < 64) ? static_cast<int>((rest >> i) & 1) : 0);
		}

		bytes.insert(bytes.end(), 8, 0x00);

		return bytes;
	}
}  // namespace

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

// 31 leading zeros put codeNum at 2^31 - 1 + rest, so 0xFFFFFFFE is the largest code that still
// decodes. It pins the boundary the range check below must not move.
TEST(NalUnitBitstreamParser, ReadsTheLargestExpGolombCode)
{
	const auto bitstream = MakeUevBitstream(31, 0x7FFFFFFF);
	NalUnitBitstreamParser parser(bitstream.data(), bitstream.size());

	uint32_t value = 0;
	ASSERT_TRUE(parser.ReadUEV(value));
	EXPECT_EQ(value, 0xFFFFFFFEU);
}

// 32 leading zeros put codeNum at 2^32 - 1 + rest, past what the caller can hold
TEST(NalUnitBitstreamParser, RejectsAnExpGolombCodeOutOfTheUint32Range)
{
	const auto bitstream = MakeUevBitstream(32, 1);
	NalUnitBitstreamParser parser(bitstream.data(), bitstream.size());

	uint32_t value = 0;
	EXPECT_FALSE(parser.ReadUEV(value));
}

// A zero run this long once wrapped the bit count into the uint8_t width parameter of ReadBits(),
// which reported success and left the bit position past the code it never decoded
TEST(NalUnitBitstreamParser, RejectsALongZeroRunInsteadOfWrappingTheBitCount)
{
	for (int leading_zero_bits : {33, 255, 256, 264})
	{
		const auto bitstream = MakeUevBitstream(leading_zero_bits, 0);
		NalUnitBitstreamParser parser(bitstream.data(), bitstream.size());

		uint32_t value = 0;
		EXPECT_FALSE(parser.ReadUEV(value)) << "leading_zero_bits: " << leading_zero_bits;
	}
}
