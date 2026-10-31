//==============================================================================
//
//  OvenMediaEngine
//
//  Copyright (c) 2026 OvenMediaLabs. All rights reserved.
//
//==============================================================================
#include <gtest/gtest.h>

#include <memory>

#include "rtp_header_extension/rtp_header_extension_abs_send_time.h"
#include "rtp_header_extension/rtp_header_extensions.h"
#include "rtp_packet.h"

namespace
{
	constexpr uint8_t kExtensionCount = 20;

	RtpHeaderExtensions MakeExtensions()
	{
		RtpHeaderExtensions extensions;
		for (uint8_t id = 1; id <= kExtensionCount; id++)
		{
			auto extension = std::make_shared<RtpHeaderExtensionAbsSendTime>(id);
			extension->SetAbsSendTime(id);
			EXPECT_TRUE(extensions.AddExtention(extension));
		}
		return extensions;
	}

	void ExpectEveryExtension(const RtpPacket &packet)
	{
		for (uint8_t id = 1; id <= kExtensionCount; id++)
		{
			SCOPED_TRACE(id);
			auto *extension = packet.Extension(id);
			ASSERT_NE(extension, nullptr);
			// Two-byte header element: id, length, data
			EXPECT_EQ(extension[0], id);
		}
		EXPECT_EQ(packet.Extension(kExtensionCount + 1), nullptr);
	}
}  // namespace

TEST(RtpPacketExtensions, EveryExtensionIsFoundAfterSetAndCopy)
{
	RtpPacket packet;
	packet.SetExtensions(MakeExtensions());
	ExpectEveryExtension(packet);

	RtpPacket copy(packet);
	ExpectEveryExtension(copy);
}
