//==============================================================================
//
//  OvenMediaEngine - Unit Tests
//
//  tests/base/ovlibrary/test_bit_reader.cpp
//  Covers: BitReader (byte reads, bit reads, skip, remained)
//
//==============================================================================
#include <gtest/gtest.h>

#include <base/ovlibrary/bit_reader.h>

// ---------------------------------------------------------------------------
// ReadBytes (big-endian)
// ---------------------------------------------------------------------------

TEST(BitReader, ReadUint8BigEndian)
{
	const uint8_t buf[] = {0xAB};
	BitReader r(buf, sizeof(buf));
	EXPECT_EQ(r.ReadBytes<uint8_t>(), 0xABu);
}

TEST(BitReader, ReadUint16BigEndian)
{
	const uint8_t buf[] = {0x01, 0x02};
	BitReader r(buf, sizeof(buf));
	EXPECT_EQ(r.ReadBytes<uint16_t>(), 0x0102u);
}

TEST(BitReader, ReadUint32BigEndian)
{
	const uint8_t buf[] = {0xDE, 0xAD, 0xBE, 0xEF};
	BitReader r(buf, sizeof(buf));
	EXPECT_EQ(r.ReadBytes<uint32_t>(), 0xDEADBEEFu);
}

TEST(BitReader, ReadUint32LittleEndian)
{
	const uint8_t buf[] = {0xEF, 0xBE, 0xAD, 0xDE};
	BitReader r(buf, sizeof(buf));
	EXPECT_EQ(r.ReadBytes<uint32_t>(false), 0xDEADBEEFu);
}

TEST(BitReader, ReadBeyondCapacityReturnsZero)
{
	const uint8_t buf[] = {0x01};
	BitReader r(buf, sizeof(buf));
	// Already consumed 1 byte
	r.ReadBytes<uint8_t>();
	EXPECT_EQ(r.ReadBytes<uint16_t>(), 0u);
}

TEST(BitReader, ReadMultipleSequential)
{
	const uint8_t buf[] = {0x01, 0x02, 0x03, 0x04, 0x05};
	BitReader r(buf, sizeof(buf));
	EXPECT_EQ(r.ReadBytes<uint8_t>(), 0x01u);
	EXPECT_EQ(r.ReadBytes<uint8_t>(), 0x02u);
	EXPECT_EQ(r.ReadBytes<uint16_t>(), 0x0304u);
	EXPECT_EQ(r.ReadBytes<uint8_t>(), 0x05u);
}

// ---------------------------------------------------------------------------
// SkipBytes / BytesRemained
// ---------------------------------------------------------------------------

TEST(BitReader, SkipBytes)
{
	const uint8_t buf[] = {0x00, 0x00, 0xAB};
	BitReader r(buf, sizeof(buf));
	r.SkipBytes(2);
	EXPECT_EQ(r.ReadBytes<uint8_t>(), 0xABu);
}

TEST(BitReader, SkipBeyondCapacityReturnsFalse)
{
	const uint8_t buf[] = {0x01, 0x02};
	BitReader r(buf, sizeof(buf));
	EXPECT_FALSE(r.SkipBytes(100));
}

TEST(BitReader, BytesRemained)
{
	const uint8_t buf[] = {1, 2, 3, 4};
	BitReader r(buf, sizeof(buf));
	EXPECT_EQ(r.BytesRemained(), 4u);
	r.ReadBytes<uint16_t>();
	EXPECT_EQ(r.BytesRemained(), 2u);
}

TEST(BitReader, SkipAll)
{
	const uint8_t buf[] = {1, 2, 3};
	BitReader r(buf, sizeof(buf));
	EXPECT_TRUE(r.SkipAll());
	EXPECT_EQ(r.BytesRemained(), 0u);
}

// ---------------------------------------------------------------------------
// ReadBits
// ---------------------------------------------------------------------------

TEST(BitReader, ReadSingleBit)
{
	// 0b10000000
	const uint8_t buf[] = {0x80};
	BitReader r(buf, sizeof(buf));
	uint8_t bit = 0;
	EXPECT_TRUE(r.ReadBit(bit));
	EXPECT_EQ(bit, 1u);
}

TEST(BitReader, ReadNibble)
{
	// 0b10110000 => high nibble = 0b1011 = 11
	const uint8_t buf[] = {0xB0};
	BitReader r(buf, sizeof(buf));
	EXPECT_EQ(r.ReadBits<uint8_t>(4), 0x0Bu);
}

TEST(BitReader, ReadBitsAcrossBytes)
{
	// 0x01 0x80 => 0b 0000_0001 1000_0000
	// Reading 9 bits starting at bit 0: 0b0_0000_0011 = 3? No...
	// Actually 0b 0000_0001 1 = 3 (first 9 bits of the stream)
	const uint8_t buf[] = {0x01, 0x80};
	BitReader r(buf, sizeof(buf));
	uint16_t val = r.ReadBits<uint16_t>(9);
	EXPECT_EQ(val, 0x03u);  // 0b 0_0000_0011
}

// ---------------------------------------------------------------------------
// ReadString
// ---------------------------------------------------------------------------

TEST(BitReader, ReadString)
{
	const uint8_t buf[] = {'h', 'e', 'l', 'l', 'o'};
	BitReader r(buf, sizeof(buf));
	auto s = r.ReadString(5);
	EXPECT_STREQ(s.CStr(), "hello");
}

TEST(BitReader, ReadStringPartial)
{
	const uint8_t buf[] = {'a', 'b', 'c', 'd'};
	BitReader r(buf, sizeof(buf));
	auto s = r.ReadString(2);
	EXPECT_STREQ(s.CStr(), "ab");
	EXPECT_EQ(r.BytesRemained(), 2u);
}

// ---------------------------------------------------------------------------
// Construction from ov::Data
// ---------------------------------------------------------------------------

TEST(BitReader, ConstructFromOvData)
{
	const uint8_t raw[] = {0x12, 0x34};
	auto data = std::make_shared<ov::Data>(raw, sizeof(raw));
	BitReader r(data);
	EXPECT_EQ(r.ReadBytes<uint16_t>(), 0x1234u);
}
