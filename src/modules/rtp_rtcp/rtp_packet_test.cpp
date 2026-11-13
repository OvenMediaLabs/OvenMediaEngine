//==============================================================================
//
//  OvenMediaEngine
//
//  Copyright (c) 2026 OvenMediaLabs. All rights reserved.
//
//==============================================================================
#include <gtest/gtest.h>

#include "rtp_header_extension/rtp_header_extension_abs_send_time.h"
#include "rtp_header_extension/rtp_header_extensions.h"
#include "rtp_packet.h"

namespace
{
	RtpHeaderExtensions MakeExtensions(uint8_t count)
	{
		RtpHeaderExtensions extensions;
		for (uint8_t id = 1; id <= count; ++id)
		{
			auto extension = std::make_shared<RtpHeaderExtensionAbsSendTime>(id);
			extension->SetAbsSendTime(id);
			EXPECT_TRUE(extensions.AddExtention(extension));
		}
		return extensions;
	}

	void ExpectExtensions(const RtpPacket &packet, uint8_t count)
	{
		for (uint8_t id = 1; id <= count; ++id)
		{
			SCOPED_TRACE(id);
			auto extension = packet.Extension(id);
			ASSERT_NE(extension, nullptr);
			// Two-byte element header followed by the three-byte absolute send time.
			EXPECT_EQ(extension[0], id);
			EXPECT_EQ(extension[1], 3);
			EXPECT_EQ(ByteReader<uint24_t>::ReadBigEndian(extension + 2), id);
		}
		EXPECT_EQ(packet.Extension(0), nullptr);
		EXPECT_EQ(packet.Extension(count + 1), nullptr);
	}
}  // namespace

TEST(RtpPacketExtensions, CopiesInlineAndOverflowExtensionsIndependently)
{
	for (uint8_t count : {3, 14, 20})
	{
		SCOPED_TRACE(count);
		RtpPacket packet;
		packet.SetExtensions(MakeExtensions(count));
		ExpectExtensions(packet, count);

		RtpPacket copy(packet);
		ExpectExtensions(copy, count);
		EXPECT_EQ(copy.GetData()->ToString(), packet.GetData()->ToString());

		// Updating the copied packet's extensions must not change the original.
		for (uint8_t id : {uint8_t{1}, count})
		{
			ASSERT_NE(copy.Extension(id), nullptr);
			copy.Extension(id)[4] = 0;
			EXPECT_EQ(packet.Extension(id)[4], id);
		}
	}
}

TEST(RtpPacketExtensions, ReplacingExtensionsRemovesOldOffsets)
{
	RtpPacket packet;
	packet.SetExtensions(MakeExtensions(20));
	packet.SetExtensions(MakeExtensions(3));
	ExpectExtensions(packet, 3);
	for (uint8_t id = 4; id <= 20; ++id)
	{
		EXPECT_EQ(packet.Extension(id), nullptr);
	}

	packet.SetExtensions(MakeExtensions(0));
	for (uint8_t id = 1; id <= 20; ++id)
	{
		EXPECT_EQ(packet.Extension(id), nullptr);
	}
}
