//==============================================================================
//
//  OvenMediaEngine - Unit Tests
//
//  tests/base/ovlibrary/test_hex.cpp
//  Covers: ov::Hex (Encode / Decode)
//
//==============================================================================
#include <gtest/gtest.h>

#include <base/ovlibrary/hex.h>

// ---------------------------------------------------------------------------
// Encode
// ---------------------------------------------------------------------------

TEST(OvHex, EncodeKnownBytes)
{
	const uint8_t buf[] = {0xDE, 0xAD, 0xBE, 0xEF};
	auto hex = ov::Hex::Encode(buf, sizeof(buf));
	// Should be "deadbeef" (lowercase)
	ov::String lower(hex);
	lower.MakeLower();
	EXPECT_STREQ(lower.CStr(), "deadbeef");
}

TEST(OvHex, EncodeAllZero)
{
	const uint8_t buf[] = {0x00, 0x00};
	auto hex = ov::Hex::Encode(buf, sizeof(buf));
	ov::String lower(hex);
	lower.MakeLower();
	EXPECT_STREQ(lower.CStr(), "0000");
}

TEST(OvHex, EncodeAllFF)
{
	const uint8_t buf[] = {0xFF, 0xFF};
	auto hex = ov::Hex::Encode(buf, sizeof(buf));
	ov::String lower(hex);
	lower.MakeLower();
	EXPECT_STREQ(lower.CStr(), "ffff");
}

TEST(OvHex, EncodeFromDataObject)
{
	const uint8_t buf[] = {0x01, 0x02, 0x03};
	auto data = std::make_shared<ov::Data>(buf, sizeof(buf));
	auto hex = ov::Hex::Encode(data);
	ov::String lower(hex);
	lower.MakeLower();
	EXPECT_STREQ(lower.CStr(), "010203");
}

TEST(OvHex, EncodeSingleByte)
{
	const uint8_t buf[] = {0x0F};
	auto hex = ov::Hex::Encode(buf, sizeof(buf));
	ov::String lower(hex);
	lower.MakeLower();
	EXPECT_STREQ(lower.CStr(), "0f");
}

// ---------------------------------------------------------------------------
// Decode
// ---------------------------------------------------------------------------

TEST(OvHex, DecodeLowercase)
{
	auto data = ov::Hex::Decode("deadbeef");
	ASSERT_NE(data, nullptr);
	EXPECT_EQ(data->GetLength(), 4u);
	EXPECT_EQ(data->At(0), 0xDEu);
	EXPECT_EQ(data->At(1), 0xADu);
	EXPECT_EQ(data->At(2), 0xBEu);
	EXPECT_EQ(data->At(3), 0xEFu);
}

TEST(OvHex, DecodeUppercase)
{
	auto data = ov::Hex::Decode("DEADBEEF");
	ASSERT_NE(data, nullptr);
	EXPECT_EQ(data->GetLength(), 4u);
	EXPECT_EQ(data->At(0), 0xDEu);
}

TEST(OvHex, DecodeUuidFormat)
{
	// Hyphens should be stripped automatically
	auto data = ov::Hex::Decode("550e8400-e29b-41d4-a716-446655440000");
	ASSERT_NE(data, nullptr);
	EXPECT_EQ(data->GetLength(), 16u);
}

TEST(OvHex, EncodeDecodeRoundTrip)
{
	const uint8_t original[] = {0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC, 0xDE, 0xF0};
	auto hex = ov::Hex::Encode(original, sizeof(original));
	auto decoded = ov::Hex::Decode(hex);
	ASSERT_NE(decoded, nullptr);
	ASSERT_EQ(decoded->GetLength(), sizeof(original));
	EXPECT_EQ(::memcmp(decoded->GetData(), original, sizeof(original)), 0);
}
