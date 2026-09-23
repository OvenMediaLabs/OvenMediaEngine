//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Rostyslav Reznichenko
//  Copyright (c) 2026 OvenMediaLabs. All rights reserved.
//
//==============================================================================
#include "mpegts_pes.h"

#include <gtest/gtest.h>

#include <cstdint>

namespace mpegts
{
	// A PTS/DTS field is 40 bits laid out as:
	//
	//  76543210  76543210  76543210  76543210  76543210
	// [ssssTTTm][TTTTTTTT][TTTTTTTm][TTTTTTTT][TTTTTTTm]
	//
	// s: the 4-bit prefix, m: marker bit (must be 1), T: the 33-bit timestamp
	//
	// The prefix only repeats pts_dts_flags, so the parser reads it but does not
	// validate it, it only flags a mismatch for the depacketizer to report once per
	// stream. Some encoders leak the upper bits of a timestamp wider than 33 bits into
	// it, so its value depends on how long the sender has been running
	//
	// Every vector below shares the tail 86 CD 04 09 taken from a Moblin capture and
	// varies only the prefix nibble, so each case must decode to the same value:
	//
	//   (1 << 30) | (0x86 << 22) | (0x66 << 15) | (0x04 << 7) | 0x04 == 1639121412
	constexpr int64_t CAPTURED_TIMESTAMP = 1639121412;
	constexpr uint8_t CAPTURED_TAIL[]	 = {0x86, 0xCD, 0x04, 0x09};

	constexpr uint8_t PTS_ONLY_BITS	   = 0b0010;
	constexpr uint8_t PTS_OF_PAIR_BITS = 0b0011;
	constexpr uint8_t DTS_BITS		   = 0b0001;

	// PTS-only, spec-conformant prefix 0b0010
	constexpr uint8_t STANDARD_PTS[]	 = {0x23, 0x86, 0xCD, 0x04, 0x09};
	// PTS-only, prefix 0b1010, the bytes the captured Moblin build actually sent
	constexpr uint8_t NON_STANDARD_PTS[] = {0xA3, 0x86, 0xCD, 0x04, 0x09};

	// Pes::ParseTimestamp is private; this fixture is friended by Pes so the parser can
	// be driven directly (see the friend declaration in mpegts_pes.h)
	class PesTest : public ::testing::Test
	{
	protected:
		static bool Parse(Pes &pes, const uint8_t *bytes, size_t length, uint8_t start_bits, int64_t &timestamp)
		{
			BitReader reader(bytes, length);

			return pes.ParseTimestamp(&reader, start_bits, timestamp);
		}

		static bool Parse(const uint8_t *bytes, size_t length, uint8_t start_bits, int64_t &timestamp)
		{
			Pes pes;

			return Parse(pes, bytes, length, start_bits, timestamp);
		}

		template <size_t N>
		static bool Parse(const uint8_t (&bytes)[N], uint8_t start_bits, int64_t &timestamp)
		{
			return Parse(bytes, N, start_bits, timestamp);
		}

		// Builds a field with the given prefix nibble on top of the captured tail
		static void BuildField(uint8_t prefix, uint8_t (&field)[5])
		{
			field[0] = static_cast<uint8_t>((prefix << 4) | (STANDARD_PTS[0] & 0x0F));

			for (size_t i = 0; i < sizeof(CAPTURED_TAIL); i++)
			{
				field[i + 1] = CAPTURED_TAIL[i];
			}
		}
	};

	// Baseline: a spec-conformant PTS-only prefix parses and defines the value every
	// other prefix below must match
	TEST_F(PesTest, ParsesStandardPtsPrefix)
	{
		int64_t timestamp = 0;

		EXPECT_TRUE(Parse(STANDARD_PTS, PTS_ONLY_BITS, timestamp));
		EXPECT_EQ(timestamp, CAPTURED_TIMESTAMP);
	}

	// The regression this fix is about: the captured Moblin bytes carry prefix 0b1010
	// and must decode to the same value as the conformant encoding above
	TEST_F(PesTest, AcceptsCapturedMoblinPrefix)
	{
		int64_t timestamp = 0;

		EXPECT_TRUE(Parse(NON_STANDARD_PTS, PTS_ONLY_BITS, timestamp));
		EXPECT_EQ(timestamp, CAPTURED_TIMESTAMP);
	}

	// Any prefix nibble is accepted for any expected start bits, and none of them
	// perturbs the decoded timestamp. A mismatch is only flagged on the Pes so the
	// depacketizer can report it. This covers every bit a leaking encoder can set
	TEST_F(PesTest, IgnoresPrefixNibbleForEveryExpectedStartBits)
	{
		for (uint8_t start_bits : {PTS_ONLY_BITS, PTS_OF_PAIR_BITS, DTS_BITS})
		{
			for (uint8_t prefix = 0; prefix < 16; prefix++)
			{
				uint8_t field[5];
				BuildField(prefix, field);

				Pes pes;
				int64_t timestamp = 0;
				EXPECT_TRUE(Parse(pes, field, sizeof(field), start_bits, timestamp)) << "prefix " << static_cast<int>(prefix) << ", expected " << static_cast<int>(start_bits);
				EXPECT_EQ(timestamp, CAPTURED_TIMESTAMP) << "prefix " << static_cast<int>(prefix) << ", expected " << static_cast<int>(start_bits);

				EXPECT_EQ(pes.HasNonStandardStartBits(), prefix != start_bits) << "prefix " << static_cast<int>(prefix) << ", expected " << static_cast<int>(start_bits);
				if (prefix != start_bits)
				{
					EXPECT_EQ(pes.NonStandardStartBits(), prefix);
					EXPECT_EQ(pes.ExpectedStartBits(), start_bits);
				}
			}
		}
	}

	// The three marker bits are the corruption guard and stay enforced. Clearing any
	// one of them must still fail the parse, whatever the prefix looks like
	TEST_F(PesTest, RejectsClearedMarkerBits)
	{
		int64_t timestamp = 0;

		// Marker bit of the first byte cleared (0xA3 -> 0xA2)
		const uint8_t first_byte_marker_cleared[] = {0xA2, 0x86, 0xCD, 0x04, 0x09};
		EXPECT_FALSE(Parse(first_byte_marker_cleared, PTS_ONLY_BITS, timestamp));

		// Marker bit of the third byte cleared (0xCD -> 0xCC)
		const uint8_t third_byte_marker_cleared[] = {0xA3, 0x86, 0xCC, 0x04, 0x09};
		EXPECT_FALSE(Parse(third_byte_marker_cleared, PTS_ONLY_BITS, timestamp));

		// Marker bit of the fifth byte cleared (0x09 -> 0x08)
		const uint8_t fifth_byte_marker_cleared[] = {0xA3, 0x86, 0xCD, 0x04, 0x08};
		EXPECT_FALSE(Parse(fifth_byte_marker_cleared, PTS_ONLY_BITS, timestamp));
	}

	// A field cut short must not yield a partially-assembled timestamp. BitReader returns
	// 0 once the buffer is exhausted, so the trailing marker check catches it
	TEST_F(PesTest, RejectsTruncatedField)
	{
		int64_t timestamp = 0;

		EXPECT_FALSE(Parse(NON_STANDARD_PTS, 3, PTS_ONLY_BITS, timestamp));
	}

	// Boundary: the maximum 33-bit timestamp reassembles correctly with either prefix
	TEST_F(PesTest, ParsesMaximumTimestamp)
	{
		constexpr int64_t MAX_TIMESTAMP = 0x1FFFFFFFF;
		int64_t timestamp				= 0;

		const uint8_t standard_max[]	= {0x2F, 0xFF, 0xFF, 0xFF, 0xFF};
		EXPECT_TRUE(Parse(standard_max, PTS_ONLY_BITS, timestamp));
		EXPECT_EQ(timestamp, MAX_TIMESTAMP);

		timestamp						 = 0;
		const uint8_t non_standard_max[] = {0xAF, 0xFF, 0xFF, 0xFF, 0xFF};
		EXPECT_TRUE(Parse(non_standard_max, PTS_ONLY_BITS, timestamp));
		EXPECT_EQ(timestamp, MAX_TIMESTAMP);
	}
}  // namespace mpegts
