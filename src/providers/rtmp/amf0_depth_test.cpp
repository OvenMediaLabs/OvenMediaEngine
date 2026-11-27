//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2026 OvenMediaLabs. All rights reserved.
//
//==============================================================================
#include <gtest/gtest.h>

#include <memory>
#include <vector>

#include "../../modules/rtmp/amf0/amf_document.h"
#include "../../modules/rtmp_v2/amf0/amf_document.h"

namespace
{
	enum class Nesting
	{
		Object,
		EcmaArray,
		StrictArray,
	};

	// Depth that real metadata never reaches and that stays well inside the decoder's depth limit
	constexpr size_t SHALLOW_DEPTH = 16;

	// Several times the depth that overflows an 8 MB stack,
	// and still under the 16 MB RTMP message limit (5 to 11 bytes per level)
	constexpr size_t DEEP_DEPTH	   = 200000;

	// `rtmp` (global namespace) and `rtmp_v2` (modules::rtmp) carry separate copies of the decoder
	constexpr size_t MaxDecodeDepth(const ::AmfDocument *)
	{
		return ::AmfPropertyBase::MAX_DECODE_DEPTH;
	}

	constexpr size_t MaxDecodeDepth(const modules::rtmp::AmfDocument *)
	{
		return modules::rtmp::AmfPropertyBase::MAX_DECODE_DEPTH;
	}

	void Append(std::vector<uint8_t> &out, std::initializer_list<uint8_t> bytes)
	{
		out.insert(out.end(), bytes.begin(), bytes.end());
	}

	// "connect" followed by a value nested `depth` levels deep, with every container properly closed:
	//   Object      : 03 { 00 01 'a' <child> } 00 00 09
	//   EcmaArray   : 08 00 00 00 01 { 00 01 'a' <child> } 00 00 09
	//   StrictArray : 0A 00 00 00 01 <child>
	// The innermost value is Null (05).
	std::vector<uint8_t> MakeNestedPayload(Nesting nesting, size_t depth)
	{
		std::vector<uint8_t> payload;
		Append(payload, {0x02, 0x00, 0x07, 'c', 'o', 'n', 'n', 'e', 'c', 't'});

		std::vector<uint8_t> suffix;

		for (size_t i = 0; i < depth; i++)
		{
			switch (nesting)
			{
				case Nesting::Object:
					Append(payload, {0x03, 0x00, 0x01, 'a'});
					Append(suffix, {0x09, 0x00, 0x00});
					break;

				case Nesting::EcmaArray:
					Append(payload, {0x08, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 'a'});
					Append(suffix, {0x09, 0x00, 0x00});
					break;

				case Nesting::StrictArray:
					Append(payload, {0x0A, 0x00, 0x00, 0x00, 0x01});
					break;
			}
		}

		Append(payload, {0x05});

		// Reversing the whole suffix turns each stored "09 00 00" into "00 00 09"
		for (size_t i = suffix.size(); i > 0; i--)
		{
			payload.push_back(suffix[i - 1]);
		}

		return payload;
	}
}  // namespace

template <typename DocumentT>
class AmfDepthTest : public ::testing::Test
{
protected:
	static constexpr size_t MaxDepth()
	{
		return MaxDecodeDepth(static_cast<const DocumentT *>(nullptr));
	}

	bool Decode(Nesting nesting, size_t depth)
	{
		auto payload = MakeNestedPayload(nesting, depth);
		auto data	 = std::make_shared<ov::Data>(payload.data(), payload.size());
		ov::ByteStream byte_stream(data);

		DocumentT document;
		return document.Decode(byte_stream);
	}
};

using AmfDocumentTypes = ::testing::Types<::AmfDocument, modules::rtmp::AmfDocument>;
TYPED_TEST_SUITE(AmfDepthTest, AmfDocumentTypes);

TYPED_TEST(AmfDepthTest, ShallowNestingIsDecoded)
{
	EXPECT_TRUE(this->Decode(Nesting::Object, SHALLOW_DEPTH));
	EXPECT_TRUE(this->Decode(Nesting::EcmaArray, SHALLOW_DEPTH));
	EXPECT_TRUE(this->Decode(Nesting::StrictArray, SHALLOW_DEPTH));
}

TYPED_TEST(AmfDepthTest, NestingAtTheLimitIsDecoded)
{
	EXPECT_TRUE(this->Decode(Nesting::Object, this->MaxDepth()));
	EXPECT_TRUE(this->Decode(Nesting::EcmaArray, this->MaxDepth()));
	EXPECT_TRUE(this->Decode(Nesting::StrictArray, this->MaxDepth()));
}

TYPED_TEST(AmfDepthTest, NestingOneOverTheLimitIsRejected)
{
	EXPECT_FALSE(this->Decode(Nesting::Object, this->MaxDepth() + 1));
	EXPECT_FALSE(this->Decode(Nesting::EcmaArray, this->MaxDepth() + 1));
	EXPECT_FALSE(this->Decode(Nesting::StrictArray, this->MaxDepth() + 1));
}

// Far beyond the limit, decoding must fail instead of overflowing the stack
TYPED_TEST(AmfDepthTest, ExcessiveObjectNestingIsRejected)
{
	EXPECT_FALSE(this->Decode(Nesting::Object, DEEP_DEPTH));
}

TYPED_TEST(AmfDepthTest, ExcessiveEcmaArrayNestingIsRejected)
{
	EXPECT_FALSE(this->Decode(Nesting::EcmaArray, DEEP_DEPTH));
}

TYPED_TEST(AmfDepthTest, ExcessiveStrictArrayNestingIsRejected)
{
	EXPECT_FALSE(this->Decode(Nesting::StrictArray, DEEP_DEPTH));
}
