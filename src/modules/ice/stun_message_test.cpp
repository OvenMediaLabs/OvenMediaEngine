//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2026 OvenMediaLabs. All rights reserved.
//
//==============================================================================
// Covers: StunMessage::Parse() rejection of attributes whose declared length
// does not fit the attribute type (REQUESTED-TRANSPORT, ERROR-CODE, fixed-size attributes)
#include "stun/stun_message.h"

#include <base/ovlibrary/ovlibrary.h>
#include <gtest/gtest.h>

namespace
{
	constexpr uint16_t kAllocateRequest	   = 0x0003;
	constexpr uint16_t kMessageIntegrity   = 0x0008;
	constexpr uint16_t kErrorCode		   = 0x0009;
	constexpr uint16_t kRequestedTransport = 0x0019;
	constexpr uint16_t kPriority		   = 0x0024;

	void AppendBE16(std::vector<uint8_t> &buffer, uint16_t value)
	{
		buffer.push_back(static_cast<uint8_t>(value >> 8));
		buffer.push_back(static_cast<uint8_t>(value & 0xFF));
	}

	// Builds a STUN message with a single attribute. The attribute header declares
	// `declared_length`, while `value` holds the bytes that actually follow the header.
	std::vector<uint8_t> MakeMessage(uint16_t attribute_type, uint16_t declared_length, const std::vector<uint8_t> &value)
	{
		std::vector<uint8_t> buffer;

		AppendBE16(buffer, kAllocateRequest);
		AppendBE16(buffer, static_cast<uint16_t>(4 + value.size()));
		AppendBE16(buffer, 0x2112);
		AppendBE16(buffer, 0xA442);

		for (uint8_t i = 0; i < 12; i++)
		{
			buffer.push_back(i);
		}

		AppendBE16(buffer, attribute_type);
		AppendBE16(buffer, declared_length);
		buffer.insert(buffer.end(), value.begin(), value.end());

		return buffer;
	}

	bool Parse(const std::vector<uint8_t> &packet)
	{
		ov::Data data(packet.data(), packet.size());
		ov::ByteStream stream(&data);
		StunMessage message;

		return message.Parse(stream);
	}
}  // namespace

TEST(StunMessageTest, RequestedTransportWithoutValueIsRejected)
{
	EXPECT_FALSE(Parse(MakeMessage(kRequestedTransport, 0, {})));
}

TEST(StunMessageTest, RequestedTransportShorterThanValueIsRejected)
{
	EXPECT_FALSE(Parse(MakeMessage(kRequestedTransport, 0, {17, 0, 0, 0})));
}

TEST(StunMessageTest, RequestedTransportWithValidLengthIsAccepted)
{
	EXPECT_TRUE(Parse(MakeMessage(kRequestedTransport, 4, {17, 0, 0, 0})));
}

TEST(StunMessageTest, ErrorCodeWithoutValueIsRejected)
{
	EXPECT_FALSE(Parse(MakeMessage(kErrorCode, 0, {})));
}

TEST(StunMessageTest, ErrorCodeShorterThanFixedFieldsIsRejected)
{
	for (uint16_t declared_length = 0; declared_length < 4; declared_length++)
	{
		EXPECT_FALSE(Parse(MakeMessage(kErrorCode, declared_length, {0, 0, 4, 1}))) << "declared length: " << declared_length;
	}
}

TEST(StunMessageTest, ErrorCodeWithReasonIsAccepted)
{
	EXPECT_TRUE(Parse(MakeMessage(kErrorCode, 8, {0, 0, 4, 1, 'N', 'o', 'p', 'e'})));
}

TEST(StunMessageTest, ErrorCodeWithoutReasonIsAccepted)
{
	EXPECT_TRUE(Parse(MakeMessage(kErrorCode, 4, {0, 0, 4, 1})));
}

TEST(StunMessageTest, RequestedTransportLongerThanValueIsAccepted)
{
	EXPECT_TRUE(Parse(MakeMessage(kRequestedTransport, 8, {17, 0, 0, 0, 1, 2, 3, 4})));
}

TEST(StunMessageTest, ErrorCodeWithPaddedReasonIsAccepted)
{
	EXPECT_TRUE(Parse(MakeMessage(kErrorCode, 5, {0, 0, 4, 1, 'N', 0, 0, 0})));
}

TEST(StunMessageTest, FixedSizeAttributeShorterThanValueIsRejected)
{
	EXPECT_FALSE(Parse(MakeMessage(kPriority, 0, {0, 0, 0, 1})));
	EXPECT_FALSE(Parse(MakeMessage(kMessageIntegrity, 8, std::vector<uint8_t>(20, 0))));
}

TEST(StunMessageTest, FixedSizeAttributeWithValidLengthIsAccepted)
{
	EXPECT_TRUE(Parse(MakeMessage(kPriority, 4, {0, 0, 0, 1})));
	EXPECT_TRUE(Parse(MakeMessage(kMessageIntegrity, 20, std::vector<uint8_t>(20, 0))));
}

TEST(StunMessageTest, FixedSizeAttributeLongerThanValueIsAccepted)
{
	EXPECT_TRUE(Parse(MakeMessage(kPriority, 8, {0, 0, 0, 1, 9, 9, 9, 9})));
}
