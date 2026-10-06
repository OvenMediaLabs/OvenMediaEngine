//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2026 OvenMediaLabs. All rights reserved.
//
//==============================================================================
#include "ovt_packet.h"

#include <base/ovlibrary/byte_io.h>
#include <gtest/gtest.h>

#include <vector>

// The 18-byte OVT header as the depacketizer sees it: what is accepted, what is refused,
// and which of the two refusals leaves the header "available" (waiting for more bytes).

namespace
{
	std::vector<uint8_t> Header(uint8_t first_byte, uint8_t payload_type, uint16_t sequence, uint64_t timestamp, uint32_t session_id, uint16_t payload_length)
	{
		std::vector<uint8_t> h(OVT_FIXED_HEADER_SIZE, 0);
		h[0] = first_byte;
		h[1] = payload_type;
		ByteWriter<uint16_t>::WriteBigEndian(&h[2], sequence);
		ByteWriter<uint64_t>::WriteBigEndian(&h[4], timestamp);
		ByteWriter<uint32_t>::WriteBigEndian(&h[12], session_id);
		ByteWriter<uint16_t>::WriteBigEndian(&h[16], payload_length);
		return h;
	}

	ov::Data AsData(const std::vector<uint8_t> &bytes)
	{
		return ov::Data(bytes.data(), bytes.size());
	}
}  // namespace

TEST(OvtPacketTest, FreshPacketHasNoHeader)
{
	OvtPacket packet;
	EXPECT_FALSE(packet.IsHeaderAvailable());
	EXPECT_FALSE(packet.IsPacketAvailable());
	EXPECT_EQ(packet.Version(), OVT_VERSION);
	EXPECT_EQ(packet.GetDataLength(), static_cast<size_t>(OVT_FIXED_HEADER_SIZE));
}

TEST(OvtPacketTest, LoadHeaderReadsEveryField)
{
	auto bytes = Header(0x60, 30, 0x1234, 0x0123456789ABCDEFULL, 0xDEADBEEF, 100);
	OvtPacket packet;

	ASSERT_TRUE(packet.LoadHeader(AsData(bytes)));
	EXPECT_TRUE(packet.IsHeaderAvailable());
	EXPECT_FALSE(packet.IsPacketAvailable());  // payload not yet loaded
	EXPECT_EQ(packet.Version(), 1);
	EXPECT_TRUE(packet.Marker());
	EXPECT_EQ(packet.PayloadType(), 30);
	EXPECT_EQ(packet.SequenceNumber(), 0x1234);
	EXPECT_EQ(packet.Timestamp(), 0x0123456789ABCDEFULL);
	EXPECT_EQ(packet.SessionId(), 0xDEADBEEFu);
	EXPECT_EQ(packet.PayloadLength(), 100);
	EXPECT_EQ(packet.PacketLength(), 118u);
}

TEST(OvtPacketTest, ShortBufferIsNotAHeader)
{
	auto bytes = Header(0x60, 30, 0, 0, 0, 0);
	bytes.resize(OVT_FIXED_HEADER_SIZE - 1);
	OvtPacket packet;

	EXPECT_FALSE(packet.LoadHeader(AsData(bytes)));
	EXPECT_FALSE(packet.IsHeaderAvailable());
}

// Every V other than 1 is refused, and the refusal leaves no header behind so the depacketizer drops the connection
TEST(OvtPacketTest, OtherVersionsAreRefused)
{
	for (uint8_t v : {0, 2, 3})
	{
		auto bytes = Header(static_cast<uint8_t>((v << 6) | 0x20), 30, 0, 0, 0, 4);
		OvtPacket packet;

		EXPECT_FALSE(packet.LoadHeader(AsData(bytes))) << "V=" << static_cast<int>(v);
		EXPECT_FALSE(packet.IsHeaderAvailable()) << "V=" << static_cast<int>(v);
	}
}

// The largest payload a packet may carry is exactly OVT_DEFAULT_MAX_PACKET_SIZE - 18
TEST(OvtPacketTest, PayloadLengthBoundary)
{
	const uint16_t max_payload = OVT_DEFAULT_MAX_PACKET_SIZE - OVT_FIXED_HEADER_SIZE;

	OvtPacket ok;
	EXPECT_TRUE(ok.LoadHeader(AsData(Header(0x60, 30, 0, 0, 0, max_payload))));
	EXPECT_EQ(ok.PayloadLength(), max_payload);

	OvtPacket too_long;
	EXPECT_FALSE(too_long.LoadHeader(AsData(Header(0x60, 30, 0, 0, 0, max_payload + 1))));
	EXPECT_FALSE(too_long.IsHeaderAvailable());
}

// `Load()` with the header but not the whole payload keeps the header available (waiting), not invalid
TEST(OvtPacketTest, PartialPayloadIsWaitingNotInvalid)
{
	auto bytes = Header(0x60, 30, 0, 0, 0, 10);
	bytes.push_back(0xAA);
	OvtPacket packet;

	EXPECT_FALSE(packet.Load(AsData(bytes)));
	EXPECT_TRUE(packet.IsHeaderAvailable());
	EXPECT_FALSE(packet.IsPacketAvailable());

	for (int i = 0; i < 9; i++)
	{
		bytes.push_back(static_cast<uint8_t>(i));
	}
	EXPECT_TRUE(packet.Load(AsData(bytes)));
	EXPECT_TRUE(packet.IsPacketAvailable());
	EXPECT_EQ(packet.Payload()[0], 0xAA);
	EXPECT_EQ(packet.Payload()[9], 8);
}

TEST(OvtPacketTest, ZeroLengthPayloadIsAvailableRightAway)
{
	OvtPacket packet;
	EXPECT_TRUE(packet.Load(AsData(Header(0x60, 10, 0, 0, 0, 0))));
	EXPECT_TRUE(packet.IsPacketAvailable());
	EXPECT_EQ(packet.PayloadLength(), 0);
}

// Setters write the wire bytes the peer reads
TEST(OvtPacketTest, SettersWriteTheHeaderBytes)
{
	OvtPacket packet;
	packet.SetPayloadType(OvtPayloadType::MessageResponse);
	packet.SetSequenceNumber(7);
	packet.SetTimestamp(1755600000000ULL);
	packet.SetSessionId(11992);
	packet.SetMarker(true);
	const uint8_t payload[3] = {1, 2, 3};
	ASSERT_TRUE(packet.SetPayload(payload, sizeof(payload)));

	auto bytes = packet.GetBuffer();
	EXPECT_EQ(bytes[0], 0x60);
	EXPECT_EQ(bytes[1], 20);
	EXPECT_EQ(ByteReader<uint16_t>::ReadBigEndian(&bytes[2]), 7);
	EXPECT_EQ(ByteReader<uint64_t>::ReadBigEndian(&bytes[4]), 1755600000000ULL);
	EXPECT_EQ(ByteReader<uint32_t>::ReadBigEndian(&bytes[12]), 11992u);
	EXPECT_EQ(ByteReader<uint16_t>::ReadBigEndian(&bytes[16]), 3);
	EXPECT_EQ(packet.GetDataLength(), static_cast<size_t>(OVT_FIXED_HEADER_SIZE + 3));
	EXPECT_EQ(packet.Payload()[2], 3);

	// Marker off clears the bit and keeps V
	packet.SetMarker(false);
	EXPECT_EQ(packet.GetBuffer()[0], 0x40);
	EXPECT_FALSE(packet.Marker());
}

// The copy the session makes before stamping its id carries everything and is independent
TEST(OvtPacketTest, CopyIsIndependent)
{
	OvtPacket original;
	original.SetPayloadType(OvtPayloadType::MediaPacket);
	original.SetSequenceNumber(3);
	original.SetMarker(true);
	const uint8_t payload[2] = {9, 8};
	ASSERT_TRUE(original.SetPayload(payload, sizeof(payload)));

	OvtPacket copy(original);
	copy.SetSessionId(42);

	EXPECT_EQ(copy.PayloadType(), 30);
	EXPECT_EQ(copy.SequenceNumber(), 3);
	EXPECT_TRUE(copy.Marker());
	EXPECT_EQ(copy.SessionId(), 42u);
	EXPECT_EQ(original.SessionId(), 0u);
	EXPECT_EQ(copy.GetDataLength(), original.GetDataLength());
	EXPECT_EQ(copy.Payload()[1], 8);
	EXPECT_NE(copy.GetBuffer(), original.GetBuffer());

	// The header queries too: this copy is what goes out on the wire
	EXPECT_TRUE(copy.IsHeaderAvailable());
	EXPECT_TRUE(copy.IsPacketAvailable());
	EXPECT_EQ(copy.Version(), original.Version());
}

// A packet the packetizer built answers the header queries, the same as one loaded off the wire
TEST(OvtPacketTest, ABuiltPacketHasItsHeader)
{
	OvtPacket packet;
	EXPECT_FALSE(packet.IsHeaderAvailable());
	EXPECT_FALSE(packet.IsPacketAvailable());

	packet.SetPayloadType(OvtPayloadType::MessageRequest);
	EXPECT_TRUE(packet.IsHeaderAvailable());
	// Still no payload, so the packet itself is not complete
	EXPECT_FALSE(packet.IsPacketAvailable());

	const uint8_t payload[1] = {7};
	ASSERT_TRUE(packet.SetPayload(payload, sizeof(payload)));
	EXPECT_TRUE(packet.IsPacketAvailable());
}

// A rejected header leaves the object saying it has none, which is how the depacketizer tells
// "cannot parse" from "not enough bytes yet"
TEST(OvtPacketTest, ARejectedHeaderClearsTheLoadedState)
{
	OvtPacket packet;
	ASSERT_TRUE(packet.LoadHeader(AsData(Header(0x40, 10, 0, 0, 0, 4))));
	EXPECT_TRUE(packet.IsHeaderAvailable());

	// V is 2, which this build does not read
	EXPECT_FALSE(packet.LoadHeader(AsData(Header(0x80, 10, 0, 0, 0, 4))));
	EXPECT_FALSE(packet.IsHeaderAvailable());
	EXPECT_FALSE(packet.IsPacketAvailable());
}

// A load onto a reused packet does not keep the previous packet's data
TEST(OvtPacketTest, ReloadReplacesEverything)
{
	OvtPacket packet;
	std::vector<uint8_t> first = Header(0x60, 10, 1, 0, 0, 2);
	first.push_back(0xAA);
	first.push_back(0xBB);
	ASSERT_TRUE(packet.Load(AsData(first)));

	std::vector<uint8_t> second = Header(0x40, 20, 2, 5, 9, 1);
	second.push_back(0xCC);
	ASSERT_TRUE(packet.Load(AsData(second)));

	EXPECT_EQ(packet.PayloadType(), 20);
	EXPECT_FALSE(packet.Marker());
	EXPECT_EQ(packet.SequenceNumber(), 2);
	EXPECT_EQ(packet.Timestamp(), 5u);
	EXPECT_EQ(packet.SessionId(), 9u);
	EXPECT_EQ(packet.PayloadLength(), 1);
	EXPECT_EQ(packet.Payload()[0], 0xCC);
	EXPECT_EQ(packet.GetDataLength(), static_cast<size_t>(OVT_FIXED_HEADER_SIZE + 1));
}

// `OvtPacket(const ov::Data &)` only calls `Load()`, which can reject the header before it assigns
// anything. Every getter must still answer from a defined state, and `IsHeaderAvailable()` must
// say no: the depacketizer decides whether to wait for more bytes or drop the connection on it.
TEST(OvtPacketTest, ARejectedLoadLeavesADefinedState)
{
	// Too short to hold a header
	{
		std::vector<uint8_t> bytes(4, 0xFF);
		OvtPacket packet(AsData(bytes));
		EXPECT_FALSE(packet.IsHeaderAvailable());
		EXPECT_FALSE(packet.IsPacketAvailable());
		EXPECT_EQ(packet.PayloadType(), 0);
		EXPECT_EQ(packet.PayloadLength(), 0);
		EXPECT_EQ(packet.SequenceNumber(), 0);
		EXPECT_EQ(packet.Timestamp(), 0u);
		EXPECT_EQ(packet.SessionId(), 0u);
	}

	// A whole header with a version this build does not speak
	{
		auto bytes = Header(0x80, 30, 1, 2, 3, 4);
		OvtPacket packet(AsData(bytes));
		EXPECT_FALSE(packet.IsHeaderAvailable());
		EXPECT_EQ(packet.PayloadType(), 0);
	}

	// Empty input
	{
		OvtPacket packet{ov::Data()};
		EXPECT_FALSE(packet.IsHeaderAvailable());
		EXPECT_EQ(packet.PayloadType(), 0);
	}
}

// `Version()` reports what the packet carried, not what this build writes.
// Reading it off the wire is what makes the version check in `LoadHeader()` observable.
TEST(OvtPacketTest, VersionComesFromTheWire)
{
	auto bytes = Header(0x60, 30, 1, 2, 3, 0);
	OvtPacket packet;
	ASSERT_TRUE(packet.LoadHeader(AsData(bytes)));
	EXPECT_EQ(packet.Version(), 1);

	// A refused version is still recorded, so a log can name what arrived
	auto other = Header(0x80, 30, 1, 2, 3, 0);
	OvtPacket refused;
	EXPECT_FALSE(refused.LoadHeader(AsData(other)));
	EXPECT_EQ(refused.Version(), 2);
	EXPECT_FALSE(refused.IsHeaderAvailable());
}
