//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2026 OvenMediaLabs. All rights reserved.
//
//==============================================================================
// Covers: StunMessage::Parse() rejection of attributes whose declared length
// does not fit the attribute type (REQUESTED-TRANSPORT, ERROR-CODE, fixed-size attributes, XOR addresses)
#include "stun/stun_message.h"

#include <base/ovlibrary/ovlibrary.h>
#include <gtest/gtest.h>

#include "stun/attributes/stun_attributes.h"

namespace
{
	// STUN message type used as the carrier of every test attribute (Allocate request).
	constexpr uint16_t ALLOCATE_REQUEST	   = 0x0003;
	// Attribute type MESSAGE-INTEGRITY (fixed 20-byte value).
	constexpr uint16_t MESSAGE_INTEGRITY   = 0x0008;
	// Attribute type ERROR-CODE (4 fixed bytes, then the reason phrase).
	constexpr uint16_t ERROR_CODE		   = 0x0009;
	// Attribute type XOR-PEER-ADDRESS.
	constexpr uint16_t XOR_PEER_ADDRESS	   = 0x0012;
	// Attribute type XOR-RELAYED-ADDRESS.
	constexpr uint16_t XOR_RELAYED_ADDRESS = 0x0016;
	// Attribute type REQUESTED-TRANSPORT (fixed 4-byte value).
	constexpr uint16_t REQUESTED_TRANSPORT = 0x0019;
	// Attribute type XOR-MAPPED-ADDRESS.
	constexpr uint16_t XOR_MAPPED_ADDRESS  = 0x0020;
	// Attribute type PRIORITY (fixed 4-byte value).
	constexpr uint16_t PRIORITY			   = 0x0024;
	// Attribute type USERNAME (variable-length text).
	constexpr uint16_t USER_NAME		   = 0x0006;
	// Attribute type REALM (variable-length text).
	constexpr uint16_t REALM			   = 0x0014;
	// Attribute type NONCE (variable-length text).
	constexpr uint16_t NONCE			   = 0x0015;
	// Attribute type SOFTWARE (variable-length text).
	constexpr uint16_t SOFTWARE			   = 0x8022;

	void AppendBE16(std::vector<uint8_t> &buffer, uint16_t value)
	{
		buffer.push_back(static_cast<uint8_t>(value >> 8));
		buffer.push_back(static_cast<uint8_t>(value & 0xFF));
	}

	struct Attribute
	{
		uint16_t type;
		uint16_t declared_length;
		std::vector<uint8_t> value;
	};

	// Builds a STUN message from attributes. Each attribute header declares `declared_length`,
	// while `value` holds the bytes that actually follow the header.
	std::vector<uint8_t> MakeMessage(const std::vector<Attribute> &attributes)
	{
		std::vector<uint8_t> body;

		for (const auto &attribute : attributes)
		{
			AppendBE16(body, attribute.type);
			AppendBE16(body, attribute.declared_length);
			body.insert(body.end(), attribute.value.begin(), attribute.value.end());
		}

		std::vector<uint8_t> buffer;

		AppendBE16(buffer, ALLOCATE_REQUEST);
		AppendBE16(buffer, static_cast<uint16_t>(body.size()));
		AppendBE16(buffer, 0x2112);
		AppendBE16(buffer, 0xA442);

		for (uint8_t i = 0; i < 12; i++)
		{
			buffer.push_back(i);
		}

		buffer.insert(buffer.end(), body.begin(), body.end());

		return buffer;
	}

	std::vector<uint8_t> MakeMessage(uint16_t attribute_type, uint16_t declared_length, const std::vector<uint8_t> &value)
	{
		return MakeMessage({{attribute_type, declared_length, value}});
	}

	bool Parse(const std::vector<uint8_t> &packet, StunMessage &message)
	{
		ov::Data data(packet.data(), packet.size());
		ov::ByteStream stream(&data);

		return message.Parse(stream);
	}

	bool Parse(const std::vector<uint8_t> &packet)
	{
		StunMessage message;

		return Parse(packet, message);
	}
}  // namespace

TEST(StunMessageTest, RequestedTransportWithoutValueIsRejected)
{
	EXPECT_FALSE(Parse(MakeMessage(REQUESTED_TRANSPORT, 0, {})));
}

TEST(StunMessageTest, RequestedTransportShorterThanValueIsRejected)
{
	EXPECT_FALSE(Parse(MakeMessage(REQUESTED_TRANSPORT, 0, {17, 0, 0, 0})));
}

TEST(StunMessageTest, RequestedTransportWithValidLengthIsAccepted)
{
	EXPECT_TRUE(Parse(MakeMessage(REQUESTED_TRANSPORT, 4, {17, 0, 0, 0})));
}

TEST(StunMessageTest, ErrorCodeWithoutValueIsRejected)
{
	EXPECT_FALSE(Parse(MakeMessage(ERROR_CODE, 0, {})));
}

TEST(StunMessageTest, ErrorCodeShorterThanFixedFieldsIsRejected)
{
	for (uint16_t declared_length = 0; declared_length < 4; declared_length++)
	{
		EXPECT_FALSE(Parse(MakeMessage(ERROR_CODE, declared_length, {0, 0, 4, 1}))) << "declared length: " << declared_length;
	}
}

TEST(StunMessageTest, ErrorCodeWithReasonIsAccepted)
{
	EXPECT_TRUE(Parse(MakeMessage(ERROR_CODE, 8, {0, 0, 4, 1, 'N', 'o', 'p', 'e'})));
}

TEST(StunMessageTest, ErrorCodeWithoutReasonIsAccepted)
{
	EXPECT_TRUE(Parse(MakeMessage(ERROR_CODE, 4, {0, 0, 4, 1})));
}

TEST(StunMessageTest, RequestedTransportLongerThanValueIsRejected)
{
	EXPECT_FALSE(Parse(MakeMessage(REQUESTED_TRANSPORT, 8, {17, 0, 0, 0, 1, 2, 3, 4})));
}

TEST(StunMessageTest, ErrorCodeWithPaddedReasonIsAccepted)
{
	EXPECT_TRUE(Parse(MakeMessage(ERROR_CODE, 5, {0, 0, 4, 1, 'N', 0, 0, 0})));
}

TEST(StunMessageTest, FixedSizeAttributeShorterThanValueIsRejected)
{
	EXPECT_FALSE(Parse(MakeMessage(PRIORITY, 0, {0, 0, 0, 1})));
	EXPECT_FALSE(Parse(MakeMessage(MESSAGE_INTEGRITY, 8, std::vector<uint8_t>(20, 0))));
}

TEST(StunMessageTest, FixedSizeAttributeWithValidLengthIsAccepted)
{
	EXPECT_TRUE(Parse(MakeMessage(PRIORITY, 4, {0, 0, 0, 1})));
	EXPECT_TRUE(Parse(MakeMessage(MESSAGE_INTEGRITY, 20, std::vector<uint8_t>(20, 0))));
}

TEST(StunMessageTest, FixedSizeAttributeLongerThanValueIsRejected)
{
	EXPECT_FALSE(Parse(MakeMessage(PRIORITY, 8, {0, 0, 0, 1, 9, 9, 9, 9})));
}

TEST(StunMessageTest, XorAddressWithInvalidFamilyIsRejected)
{
	for (const auto type : {XOR_MAPPED_ADDRESS, XOR_PEER_ADDRESS, XOR_RELAYED_ADDRESS})
	{
		EXPECT_FALSE(Parse(MakeMessage(type, 8, std::vector<uint8_t>(8, 0)))) << "type: " << type;
	}
}

TEST(StunMessageTest, XorAddressWithValidFamilyIsAccepted)
{
	EXPECT_TRUE(Parse(MakeMessage(XOR_MAPPED_ADDRESS, 8, {0, 1, 0x11, 0x2B, 0xE1, 0x12, 0xA4, 0x43})));
}

TEST(StunMessageTest, XorAddressLongerThanValueIsRejected)
{
	EXPECT_FALSE(Parse(MakeMessage(XOR_MAPPED_ADDRESS, 12, {0, 1, 0x11, 0x2B, 0xE1, 0x12, 0xA4, 0x43, 9, 9, 9, 9})));
}

TEST(StunMessageTest, EmptyTextAttributesAreAccepted)
{
	for (const auto type : {USER_NAME, REALM, NONCE, SOFTWARE})
	{
		EXPECT_TRUE(Parse(MakeMessage(type, 0, {}))) << "type: " << type;
	}
}

TEST(StunMessageTest, TextAttributeKeepsItsValue)
{
	StunMessage message;

	ASSERT_TRUE(Parse(MakeMessage(USER_NAME, 5, {'a', 'b', 'c', 'd', 'e', 0, 0, 0}), message));

	auto user_name = message.GetAttribute<StunUserNameAttribute>(StunAttributeType::UserName);
	ASSERT_NE(user_name, nullptr);
	EXPECT_STREQ(user_name->GetValue().CStr(), "abcde");
}

TEST(StunMessageTest, NextAttributeStartsAfterFixedSizeAttribute)
{
	StunMessage message;

	ASSERT_TRUE(Parse(MakeMessage({{PRIORITY, 4, {0, 0, 0, 1}},
								   {USER_NAME, 4, {'a', 'b', 'c', 'd'}}}),
					  message));

	auto user_name = message.GetAttribute<StunUserNameAttribute>(StunAttributeType::UserName);
	ASSERT_NE(user_name, nullptr);
	EXPECT_STREQ(user_name->GetValue().CStr(), "abcd");
}

TEST(StunMessageTest, NextAttributeStartsAfterPaddedAttribute)
{
	StunMessage message;

	ASSERT_TRUE(Parse(MakeMessage({{USER_NAME, 5, {'a', 'b', 'c', 'd', 'e', 0, 0, 0}},
								   {PRIORITY, 4, {0, 0, 0, 1}}}),
					  message));

	auto priority = message.GetAttribute<StunPriorityAttribute>(StunAttributeType::Priority);
	ASSERT_NE(priority, nullptr);
	EXPECT_EQ(priority->GetValue(), 1U);
}

TEST(StunMessageTest, AttributeLengthThatWrapsWhenPaddedIsRejected)
{
	for (const uint16_t declared_length : {0xFFFD, 0xFFFE, 0xFFFF})
	{
		EXPECT_FALSE(Parse(MakeMessage(PRIORITY, declared_length, {0, 0, 0, 1}))) << "declared length: " << declared_length;
	}
}
