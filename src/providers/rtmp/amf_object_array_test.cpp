//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2026 OvenMediaLabs. All rights reserved.
//
//==============================================================================
#include "../../modules/rtmp/amf0/amf_object_array.h"

#include <gtest/gtest.h>

#include <vector>

#include "../../modules/rtmp_v2/amf0/amf_object_array.h"

namespace
{
	constexpr uint8_t AMF_NUMBER	 = 0x00;
	constexpr uint8_t AMF_OBJECT	 = 0x03;
	constexpr uint8_t AMF_ECMA_ARRAY = 0x08;
	constexpr uint8_t AMF_OBJECT_END = 0x09;

	template <typename Tarray>
	bool Decode(const std::vector<uint8_t> &bytes)
	{
		ov::ByteStream byte_stream(std::make_shared<const ov::Data>(bytes.data(), bytes.size()));

		Tarray array;
		return array.Decode(byte_stream, true);
	}

	// An object body that ends with a single byte, so the 2-byte name length of the next property cannot be read.
	std::vector<uint8_t> ObjectWithOneByteLeft()
	{
		return {AMF_OBJECT, 0x00};
	}

	// A well-formed property followed by a single byte instead of the object-end-marker.
	std::vector<uint8_t> ObjectWithPropertyThenOneByteLeft()
	{
		return {AMF_OBJECT, 0x00, 0x01, 'a', AMF_NUMBER, 0, 0, 0, 0, 0, 0, 0, 0, 0x00};
	}

	std::vector<uint8_t> EcmaArrayWithOneByteLeft()
	{
		return {AMF_ECMA_ARRAY, 0x00, 0x00, 0x00, 0x00, 0x00};
	}

	std::vector<uint8_t> WellFormedObject()
	{
		return {AMF_OBJECT, 0x00, 0x01, 'a', AMF_NUMBER, 0, 0, 0, 0, 0, 0, 0, 0, 0x00, 0x00, AMF_OBJECT_END};
	}

	std::vector<uint8_t> WellFormedEmptyEcmaArray()
	{
		return {AMF_ECMA_ARRAY, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, AMF_OBJECT_END};
	}
}  // namespace

template <typename T>
class AmfObjectArrayTruncatedTest : public ::testing::Test
{
};

using AmfObjectArrayTypes = ::testing::Types<
	std::pair<AmfObject, AmfEcmaArray>,
	std::pair<modules::rtmp::AmfObject, modules::rtmp::AmfEcmaArray>>;
TYPED_TEST_SUITE(AmfObjectArrayTruncatedTest, AmfObjectArrayTypes);

TYPED_TEST(AmfObjectArrayTruncatedTest, ObjectWithOneByteLeftIsRejected)
{
	EXPECT_FALSE(Decode<typename TypeParam::first_type>(ObjectWithOneByteLeft()));
}

TYPED_TEST(AmfObjectArrayTruncatedTest, ObjectWithPropertyThenOneByteLeftIsRejected)
{
	EXPECT_FALSE(Decode<typename TypeParam::first_type>(ObjectWithPropertyThenOneByteLeft()));
}

TYPED_TEST(AmfObjectArrayTruncatedTest, EcmaArrayWithOneByteLeftIsRejected)
{
	EXPECT_FALSE(Decode<typename TypeParam::second_type>(EcmaArrayWithOneByteLeft()));
}

TYPED_TEST(AmfObjectArrayTruncatedTest, WellFormedObjectIsAccepted)
{
	EXPECT_TRUE(Decode<typename TypeParam::first_type>(WellFormedObject()));
}

TYPED_TEST(AmfObjectArrayTruncatedTest, WellFormedEmptyEcmaArrayIsAccepted)
{
	EXPECT_TRUE(Decode<typename TypeParam::second_type>(WellFormedEmptyEcmaArray()));
}
