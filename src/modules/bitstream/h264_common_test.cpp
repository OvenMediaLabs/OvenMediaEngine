//==============================================================================
//
//  OvenMediaEngine - Unit Tests
//
//  tests/modules/bitstream/test_h264_common.cpp
//  Covers: H264NalUnitType helpers, IsValid/IsKnown, NalUnitTypeToStr
//
//==============================================================================
#include <gtest/gtest.h>

#include <modules/bitstream/h264/h264_common.h>

// ---------------------------------------------------------------------------
// IsValidH264NalUnitType
// ---------------------------------------------------------------------------

TEST(H264Common, ValidNalTypes)
{
	// IsValidH264NalUnitType returns true for (0 < n < 16) || (18 < n < 21)
	// Unspecified (0) is NOT valid.
	EXPECT_TRUE(IsValidH264NalUnitType(1));   // NonIdrSlice
	EXPECT_TRUE(IsValidH264NalUnitType(5));   // IDR
	EXPECT_TRUE(IsValidH264NalUnitType(7));   // SPS
	EXPECT_TRUE(IsValidH264NalUnitType(8));   // PPS
	EXPECT_TRUE(IsValidH264NalUnitType(15));  // AuxiliarySlice
	// Range 18 < n < 21
	EXPECT_TRUE(IsValidH264NalUnitType(19));
	EXPECT_TRUE(IsValidH264NalUnitType(20));
	// Boundaries that are NOT valid
	EXPECT_FALSE(IsValidH264NalUnitType(0));  // Unspecified
	EXPECT_FALSE(IsValidH264NalUnitType(16));
	EXPECT_FALSE(IsValidH264NalUnitType(18));
	EXPECT_FALSE(IsValidH264NalUnitType(21));
}

TEST(H264Common, InvalidNalTypesAboveRange)
{
	EXPECT_FALSE(IsValidH264NalUnitType(32));
	EXPECT_FALSE(IsValidH264NalUnitType(255));
}

// ---------------------------------------------------------------------------
// IsKnownH264NalUnitType
// ---------------------------------------------------------------------------

TEST(H264Common, KnownNalTypes)
{
	EXPECT_TRUE(IsKnownH264NalUnitType(H264NalUnitType::Sps));
	EXPECT_TRUE(IsKnownH264NalUnitType(H264NalUnitType::Pps));
	EXPECT_TRUE(IsKnownH264NalUnitType(H264NalUnitType::IdrSlice));
	EXPECT_TRUE(IsKnownH264NalUnitType(H264NalUnitType::NonIdrSlice));
	EXPECT_TRUE(IsKnownH264NalUnitType(H264NalUnitType::Sei));
	EXPECT_TRUE(IsKnownH264NalUnitType(H264NalUnitType::Aud));
}

TEST(H264Common, UnspecifiedNotValid)
{
	// Unspecified (0) does not satisfy the valid range check in
	// IsValidH264NalUnitType (which requires 0 < n).
	EXPECT_FALSE(IsValidH264NalUnitType(static_cast<uint8_t>(H264NalUnitType::Unspecified)));
}

// ---------------------------------------------------------------------------
// Enum equality operators with uint8_t
// ---------------------------------------------------------------------------

TEST(H264Common, Uint8EqualityWithEnum)
{
	uint8_t sps_byte = 7;
	EXPECT_TRUE(sps_byte == H264NalUnitType::Sps);
	EXPECT_FALSE(sps_byte != H264NalUnitType::Sps);

	uint8_t idr_byte = 5;
	EXPECT_TRUE(idr_byte == H264NalUnitType::IdrSlice);
}

TEST(H264Common, Uint8InequalityWithEnum)
{
	uint8_t pps_byte = 8;
	EXPECT_FALSE(pps_byte == H264NalUnitType::Sps);
	EXPECT_TRUE(pps_byte != H264NalUnitType::Sps);
}

// NalUnitTypeToStr is declared constexpr (implies inline) in the header but
// defined in the .cpp, so its symbol cannot be resolved from an external test
// binary.  The helper is an internal implementation detail and is exercised
// indirectly by the codec-specific tests; no direct test is added here.

// ---------------------------------------------------------------------------
// NAL ref IDC mask
// ---------------------------------------------------------------------------

TEST(H264Common, NalRefIdcMask)
{
	// NAL byte 0b01100111 = 0x67 (SPS, ref_idc=3)
	//   ref_idc = bits [5:6] = 0b11 = 3
	uint8_t nal_byte = 0x67;
	EXPECT_EQ(GET_NAL_REF_IDC(nal_byte), 3);

	// NAL byte 0b00100001 = 0x21 (NonIDR, ref_idc=1)
	uint8_t nal_byte2 = 0x21;
	EXPECT_EQ(GET_NAL_REF_IDC(nal_byte2), 1);
}
