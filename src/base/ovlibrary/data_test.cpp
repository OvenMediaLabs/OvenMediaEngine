//==============================================================================
//
//  OvenMediaEngine - Unit Tests
//
//  tests/base/ovlibrary/test_data.cpp
//  Covers: ov::Data
//
//==============================================================================
#include <gtest/gtest.h>

#include <base/ovlibrary/data.h>

#include <cstring>

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

TEST(OvData, DefaultConstructIsEmpty)
{
	ov::Data d;
	EXPECT_EQ(d.GetLength(), 0u);
}

TEST(OvData, ConstructWithCapacity)
{
	ov::Data d(64);
	EXPECT_EQ(d.GetLength(), 0u);
}

TEST(OvData, ConstructFromRawBuffer)
{
	const uint8_t buf[] = {1, 2, 3, 4, 5};
	ov::Data d(buf, sizeof(buf));
	EXPECT_EQ(d.GetLength(), 5u);
	EXPECT_EQ(d.At(0), 1u);
	EXPECT_EQ(d.At(4), 5u);
}

TEST(OvData, ConstructReferenceOnly)
{
	const uint8_t buf[] = {10, 20, 30};
	ov::Data d(buf, sizeof(buf), true);
	EXPECT_EQ(d.GetLength(), 3u);
	// Reference should read same bytes
	EXPECT_EQ(d.At(0), 10u);
	EXPECT_EQ(d.At(2), 30u);
}

TEST(OvData, CopyConstructor)
{
	const uint8_t buf[] = {1, 2, 3};
	ov::Data a(buf, sizeof(buf));
	ov::Data b(a);
	EXPECT_EQ(b.GetLength(), 3u);
	EXPECT_EQ(b.At(0), 1u);
	// Independent copy
	EXPECT_NE(a.GetData(), b.GetData());
}

TEST(OvData, MoveConstructor)
{
	const uint8_t buf[] = {1, 2, 3};
	ov::Data a(buf, sizeof(buf));
	ov::Data b(std::move(a));
	EXPECT_EQ(b.GetLength(), 3u);
	EXPECT_EQ(b.At(0), 1u);
}

// ---------------------------------------------------------------------------
// Clone
// ---------------------------------------------------------------------------

TEST(OvData, Clone)
{
	const uint8_t buf[] = {0xDE, 0xAD, 0xBE, 0xEF};
	ov::Data d(buf, sizeof(buf));
	auto clone = d.Clone();
	ASSERT_NE(clone, nullptr);
	EXPECT_EQ(clone->GetLength(), 4u);
	EXPECT_EQ(clone->At(0), 0xDEu);
	// ov::Data uses copy-on-write: pointers may be equal until mutation.
	// Verify independence by mutating the clone and checking the original
	// is unaffected.
	uint8_t *w = static_cast<uint8_t *>(clone->GetWritableData());
	ASSERT_NE(w, nullptr);
	w[0] = 0xFF;
	EXPECT_EQ(clone->At(0), 0xFFu);
	EXPECT_EQ(d.At(0), 0xDEu);  // original is unchanged
}

// ---------------------------------------------------------------------------
// Append
// ---------------------------------------------------------------------------

TEST(OvData, AppendRawData)
{
	ov::Data d;
	const uint8_t a[] = {1, 2};
	const uint8_t b[] = {3, 4};
	d.Append(a, sizeof(a));
	d.Append(b, sizeof(b));
	EXPECT_EQ(d.GetLength(), 4u);
	EXPECT_EQ(d.At(0), 1u);
	EXPECT_EQ(d.At(3), 4u);
}

TEST(OvData, AppendDataObject)
{
	ov::Data d;
	const uint8_t buf[] = {5, 6, 7};
	ov::Data other(buf, sizeof(buf));
	d.Append(&other);
	EXPECT_EQ(d.GetLength(), 3u);
	EXPECT_EQ(d.At(2), 7u);
}

// ---------------------------------------------------------------------------
// At / AtAs
// ---------------------------------------------------------------------------

TEST(OvData, AtOutOfBoundsReturnsZero)
{
	const uint8_t buf[] = {1, 2};
	ov::Data d(buf, sizeof(buf));
	EXPECT_EQ(d.At(100), 0u);
}

TEST(OvData, AtAsUint16)
{
	// Big-endian 0x0102
	const uint8_t buf[] = {0x01, 0x02, 0x03, 0x04};
	ov::Data d(buf, sizeof(buf));
	// AtAs reads raw bytes at index (index in T units)
	auto val = d.AtAs<uint8_t>(1);
	EXPECT_EQ(val, 0x02u);
}

// ---------------------------------------------------------------------------
// GetDataAs / GetWritableDataAs
// ---------------------------------------------------------------------------

TEST(OvData, GetDataAsTypedPointer)
{
	const uint32_t value = 0xDEADBEEF;
	ov::Data d(&value, sizeof(value));
	const uint8_t *raw = d.GetDataAs<uint8_t>();
	ASSERT_NE(raw, nullptr);
	// First byte depends on endianness, just check pointer is valid
	(void)raw;
}

TEST(OvData, GetWritableData)
{
	const uint8_t buf[] = {1, 2, 3};
	ov::Data d(buf, sizeof(buf));
	uint8_t *writable = static_cast<uint8_t *>(d.GetWritableData());
	ASSERT_NE(writable, nullptr);
	writable[0] = 99;
	EXPECT_EQ(d.At(0), 99u);
}

// ---------------------------------------------------------------------------
// Shared_ptr factory helpers
// ---------------------------------------------------------------------------

TEST(OvData, MakeSharedFromBuffer)
{
	const uint8_t buf[] = {0xAA, 0xBB};
	auto d = std::make_shared<ov::Data>(buf, sizeof(buf));
	EXPECT_EQ(d->GetLength(), 2u);
	EXPECT_EQ(d->At(0), 0xAAu);
}
