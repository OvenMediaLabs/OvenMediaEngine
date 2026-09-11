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
	// s: the 4-bit start_bits prefix, m: marker bit (must be 1), T: the 33-bit timestamp.
	//
	// Every vector below shares the tail 86 CD 04 09 taken from a real Moblin/libsrt
	// capture and varies only the prefix nibble, so each accepted case must decode to
	// the same value:
	//
	//   (1 << 30) | (0x86 << 22) | (0x66 << 15) | (0x04 << 7) | 0x04 == 1639121412
	//
	// which proves a stray high bit in the prefix does not perturb the timestamp.
	constexpr int64_t CAPTURED_TIMESTAMP		 = 1639121412;

	// PTS-only, spec-conformant prefix 0b0010.
	constexpr uint8_t STANDARD_PTS[]			 = {0x23, 0x86, 0xCD, 0x04, 0x09};
	// PTS-only, prefix 0b1010 - what Moblin/libsrt actually sends.
	constexpr uint8_t NON_STANDARD_PTS[]		 = {0xA3, 0x86, 0xCD, 0x04, 0x09};
	// PTS of a PTS/DTS pair, prefixes 0b0011 and 0b1011.
	constexpr uint8_t STANDARD_PTS_OF_PAIR[]	 = {0x33, 0x86, 0xCD, 0x04, 0x09};
	constexpr uint8_t NON_STANDARD_PTS_OF_PAIR[] = {0xB3, 0x86, 0xCD, 0x04, 0x09};
	// DTS, prefixes 0b0001 and 0b1001.
	constexpr uint8_t STANDARD_DTS[]			 = {0x13, 0x86, 0xCD, 0x04, 0x09};
	constexpr uint8_t NON_STANDARD_DTS[]		 = {0x93, 0x86, 0xCD, 0x04, 0x09};

	// Pes::ParseTimestamp is private; this fixture is friended by Pes so the parser can
	// be driven directly and its boolean result asserted (see the friend declaration in
	// mpegts_pes.h). Same arrangement as IcePort/IcePortTest in src/modules/ice.
	class PesTest : public ::testing::Test
	{
	protected:
		static bool Parse(const uint8_t *bytes, size_t length, uint8_t start_bits, int64_t &timestamp)
		{
			BitReader reader(bytes, length);
			Pes pes;

			return pes.ParseTimestamp(&reader, start_bits, timestamp);
		}

		template <size_t N>
		static bool Parse(const uint8_t (&bytes)[N], uint8_t start_bits, int64_t &timestamp)
		{
			return Parse(bytes, N, start_bits, timestamp);
		}
	};

	// Baseline: a spec-conformant PTS-only prefix still parses, and defines the value
	// every tolerated variant below must match.
	TEST_F(PesTest, ParsesStandardPtsPrefix)
	{
		int64_t timestamp = 0;

		EXPECT_TRUE(Parse(STANDARD_PTS, 0b0010, timestamp));
		EXPECT_EQ(timestamp, CAPTURED_TIMESTAMP);
	}

	// The regression this fix is about: Moblin/libsrt sets the high bit of the prefix
	// (0b1010 instead of 0b0010). ffmpeg, SRS and srt-live-server mask the prefix off and
	// read only the timestamp bits, so OME must accept it too - and decode the same value
	// as the conformant encoding above.
	TEST_F(PesTest, ToleratesStrayHighBitInPtsPrefix)
	{
		int64_t timestamp = 0;

		EXPECT_TRUE(Parse(NON_STANDARD_PTS, 0b0010, timestamp));
		EXPECT_EQ(timestamp, CAPTURED_TIMESTAMP);
	}

	// The same leniency applies to both halves of a PTS/DTS pair, not just PTS-only.
	TEST_F(PesTest, ToleratesStrayHighBitInPtsOfPairPrefix)
	{
		int64_t timestamp = 0;

		EXPECT_TRUE(Parse(STANDARD_PTS_OF_PAIR, 0b0011, timestamp));
		EXPECT_EQ(timestamp, CAPTURED_TIMESTAMP);

		timestamp = 0;
		EXPECT_TRUE(Parse(NON_STANDARD_PTS_OF_PAIR, 0b0011, timestamp));
		EXPECT_EQ(timestamp, CAPTURED_TIMESTAMP);
	}

	TEST_F(PesTest, ToleratesStrayHighBitInDtsPrefix)
	{
		int64_t timestamp = 0;

		EXPECT_TRUE(Parse(STANDARD_DTS, 0b0001, timestamp));
		EXPECT_EQ(timestamp, CAPTURED_TIMESTAMP);

		timestamp = 0;
		EXPECT_TRUE(Parse(NON_STANDARD_DTS, 0b0001, timestamp));
		EXPECT_EQ(timestamp, CAPTURED_TIMESTAMP);
	}

	// The low 3 bits are what distinguish PTS-only (0b010), PTS-of-pair (0b011) and
	// DTS (0b001), so a mismatch there must still be rejected. Without this the fix
	// would have degraded into accepting any prefix at all.
	TEST_F(PesTest, RejectsLowBitMismatch)
	{
		int64_t timestamp = 0;

		// DTS bits where a PTS-only field was expected.
		EXPECT_FALSE(Parse(STANDARD_DTS, 0b0010, timestamp));
		// PTS-of-pair bits where a PTS-only field was expected.
		EXPECT_FALSE(Parse(STANDARD_PTS_OF_PAIR, 0b0010, timestamp));
		// PTS-only bits where the DTS half of a pair was expected.
		EXPECT_FALSE(Parse(STANDARD_PTS, 0b0001, timestamp));
	}

	// Tolerating the high bit must not weaken the low-3-bit check: a prefix that has both
	// a stray high bit and the wrong low bits is still rejected.
	TEST_F(PesTest, RejectsLowBitMismatchEvenWithStrayHighBit)
	{
		int64_t timestamp = 0;

		EXPECT_FALSE(Parse(NON_STANDARD_PTS_OF_PAIR, 0b0010, timestamp));
		EXPECT_FALSE(Parse(NON_STANDARD_DTS, 0b0011, timestamp));
		EXPECT_FALSE(Parse(NON_STANDARD_PTS, 0b0001, timestamp));
	}

	// The three marker bits are the real corruption guard, and they are untouched by this
	// change - clearing any one of them must still fail the parse.
	TEST_F(PesTest, RejectsClearedMarkerBits)
	{
		int64_t timestamp						  = 0;

		// Marker bit of the first byte cleared (0xA3 -> 0xA2).
		const uint8_t first_byte_marker_cleared[] = {0xA2, 0x86, 0xCD, 0x04, 0x09};
		EXPECT_FALSE(Parse(first_byte_marker_cleared, 0b0010, timestamp));

		// Marker bit of the third byte cleared (0xCD -> 0xCC).
		const uint8_t third_byte_marker_cleared[] = {0xA3, 0x86, 0xCC, 0x04, 0x09};
		EXPECT_FALSE(Parse(third_byte_marker_cleared, 0b0010, timestamp));

		// Marker bit of the fifth byte cleared (0x09 -> 0x08).
		const uint8_t fifth_byte_marker_cleared[] = {0xA3, 0x86, 0xCD, 0x04, 0x08};
		EXPECT_FALSE(Parse(fifth_byte_marker_cleared, 0b0010, timestamp));
	}

	// A field cut short must not yield a partially-assembled timestamp. BitReader returns
	// 0 once the buffer is exhausted, so the trailing marker check catches it.
	TEST_F(PesTest, RejectsTruncatedField)
	{
		int64_t timestamp = 0;

		EXPECT_FALSE(Parse(NON_STANDARD_PTS, 3, 0b0010, timestamp));
	}

	// Boundary: the maximum 33-bit timestamp reassembles correctly with either prefix.
	TEST_F(PesTest, ParsesMaximumTimestamp)
	{
		constexpr int64_t MAX_TIMESTAMP = 0x1FFFFFFFF;
		int64_t timestamp				= 0;

		const uint8_t standard_max[]	= {0x2F, 0xFF, 0xFF, 0xFF, 0xFF};
		EXPECT_TRUE(Parse(standard_max, 0b0010, timestamp));
		EXPECT_EQ(timestamp, MAX_TIMESTAMP);

		timestamp						 = 0;
		const uint8_t non_standard_max[] = {0xAF, 0xFF, 0xFF, 0xFF, 0xFF};
		EXPECT_TRUE(Parse(non_standard_max, 0b0010, timestamp));
		EXPECT_EQ(timestamp, MAX_TIMESTAMP);
	}
}  // namespace mpegts
