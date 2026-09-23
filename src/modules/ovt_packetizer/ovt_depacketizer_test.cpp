//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2026 OvenMediaLabs. All rights reserved.
//
//==============================================================================
#include "ovt_depacketizer.h"
#include "ovt_wire.h"

#include <base/ovlibrary/byte_io.h>
#include <gtest/gtest.h>

#include <vector>

// Input that cannot be parsed must fail `AppendPacket()` so the connection is dropped;
// returning true while leaving the bytes unconsumed would stall reception for good.

namespace
{
	constexpr uint8_t VERSION_BIT = 0x40;  // V=1 in the top two bits
	constexpr uint8_t MARKER_BIT = 0x20;

	// One raw OVT packet built without `OvtPacket`, so out-of-range header values can be produced
	std::vector<uint8_t> RawPacket(uint8_t first_byte, uint8_t payload_type, uint16_t payload_length, size_t payload_bytes)
	{
		std::vector<uint8_t> packet(OVT_FIXED_HEADER_SIZE + payload_bytes, 0);
		packet[0] = first_byte;
		packet[1] = payload_type;
		ByteWriter<uint16_t>::WriteBigEndian(&packet[16], payload_length);
		return packet;
	}

	std::vector<uint8_t> UnmarkedFragment(uint8_t payload_type, uint16_t payload_length)
	{
		return RawPacket(VERSION_BIT, payload_type, payload_length, payload_length);
	}
}  // namespace

// A header that arrives without its payload is "wait for more", whatever payload type it carries.
// Payload type 0 used to fall out of that branch and drop the connection, so the same bytes were
// refused or ignored depending on where TCP split them.
TEST(OvtDepacketizerTest, AHeaderWithoutItsPayloadWaitsWhateverThePayloadTypeIs)
{
	for (uint8_t payload_type : {uint8_t(0), uint8_t(10), uint8_t(200)})
	{
		OvtDepacketizer depacketizer;
		auto packet = RawPacket(VERSION_BIT | MARKER_BIT, payload_type, 100, 100);

		// The header alone
		ASSERT_TRUE(depacketizer.AppendPacket(packet.data(), OVT_FIXED_HEADER_SIZE))
			<< "payload type " << static_cast<int>(payload_type);
		EXPECT_FALSE(depacketizer.IsAvailable());

		// ... and the rest. A type outside the table is consumed and dropped, not refused
		ASSERT_TRUE(depacketizer.AppendPacket(packet.data() + OVT_FIXED_HEADER_SIZE, 100))
			<< "payload type " << static_cast<int>(payload_type);
	}
}

// The read side of an OVT publisher takes requests only, so a media fragment goes no further than
// the parse loop. Reassembling it first would let a peer park the media buffer limit per connection.
TEST(OvtDepacketizerTest, MessagesOnlyDropsMediaPerFragment)
{
	OvtDepacketizer depacketizer(OvtDepacketizer::MediaRole::MessagesOnly);

	// Enough unmarked media fragments to pass the reassembly limit if they were being kept
	auto fragment = UnmarkedFragment(ov::ToUnderlyingType(OvtPayloadType::MediaPacket), 8192);
	for (size_t appended = 0; appended <= MAX_MEDIA_PACKET_BUFFER_SIZE; appended += 8192)
	{
		ASSERT_TRUE(depacketizer.AppendPacket(fragment.data(), fragment.size()));
		ASSERT_FALSE(depacketizer.IsAvailable());
	}

	// A request on the same connection still comes through
	auto payload = std::vector<uint8_t>(16, 0x5A);
	auto request = RawPacket(VERSION_BIT | MARKER_BIT, ov::ToUnderlyingType(OvtPayloadType::MessageRequest),
							 payload.size(), payload.size());
	ASSERT_TRUE(depacketizer.AppendPacket(request.data(), request.size()));
	ASSERT_TRUE(depacketizer.IsAvailableMessage());
	EXPECT_EQ(depacketizer.PopMessage()->payload_type, OvtPayloadType::MessageRequest);

	// Nothing reaches the queue, so the count is the only way a consumer can see that a peer sent media.
	// Without it the origin cannot log that it happened.
	EXPECT_GT(depacketizer.GetDroppedMediaFragmentCount(), 0u);
}

// The other mode keeps media and counts nothing
TEST(OvtDepacketizerTest, MessagesAndMediaCountsNoDrop)
{
	OvtDepacketizer depacketizer;
	auto media = RawPacket(VERSION_BIT | MARKER_BIT, ov::ToUnderlyingType(OvtPayloadType::MediaPacket),
						   MEDIA_PACKET_HEADER_SIZE, MEDIA_PACKET_HEADER_SIZE);

	ASSERT_TRUE(depacketizer.AppendPacket(media.data(), media.size()));
	EXPECT_TRUE(depacketizer.IsAvailableMediaPacket());
	EXPECT_EQ(depacketizer.GetDroppedMediaFragmentCount(), 0u);
}

TEST(OvtDepacketizerTest, PayloadLengthOverLimitIsRejected)
{
	OvtDepacketizer depacketizer;
	auto packet = RawPacket(VERSION_BIT | MARKER_BIT, ov::ToUnderlyingType(OvtPayloadType::MediaPacket), 0xFFFF, 0);

	EXPECT_FALSE(depacketizer.AppendPacket(packet.data(), packet.size()));
	EXPECT_FALSE(depacketizer.IsAvailable());
}

TEST(OvtDepacketizerTest, VersionMismatchIsRejected)
{
	OvtDepacketizer depacketizer;
	auto packet = RawPacket(0x80 | MARKER_BIT, ov::ToUnderlyingType(OvtPayloadType::MessageRequest), 2, 2);

	EXPECT_FALSE(depacketizer.AppendPacket(packet.data(), packet.size()));
}

TEST(OvtDepacketizerTest, FragmentedMessageBelowLimitIsReassembled)
{
	OvtDepacketizer depacketizer;
	const uint16_t fragment_size = OVT_DEFAULT_MAX_PACKET_SIZE - OVT_FIXED_HEADER_SIZE;

	auto first = UnmarkedFragment(ov::ToUnderlyingType(OvtPayloadType::MessageResponse), fragment_size);
	ASSERT_TRUE(depacketizer.AppendPacket(first.data(), first.size()));
	EXPECT_FALSE(depacketizer.IsAvailableMessage());

	auto last = RawPacket(VERSION_BIT | MARKER_BIT, ov::ToUnderlyingType(OvtPayloadType::MessageResponse), 4, 4);
	ASSERT_TRUE(depacketizer.AppendPacket(last.data(), last.size()));
	ASSERT_TRUE(depacketizer.IsAvailableMessage());

	auto message = depacketizer.PopMessage();
	ASSERT_TRUE(message.has_value());
	EXPECT_EQ(message->payload_type, OvtPayloadType::MessageResponse);
	EXPECT_EQ(message->data->GetLength(), static_cast<size_t>(fragment_size + 4));
}

// The wire values of the payload types, written out by hand.
// Everything else names them through `OvtPayloadType`, so changing an enumerator would move the
// protocol value with every test still green. These literals are what stops that.
TEST(OvtDepacketizerTest, PayloadTypeWireValues)
{
	EXPECT_EQ(ov::ToUnderlyingType(OvtPayloadType::MessageRequest), 10);
	EXPECT_EQ(ov::ToUnderlyingType(OvtPayloadType::MessageResponse), 20);
	EXPECT_EQ(ov::ToUnderlyingType(OvtPayloadType::MediaPacket), 30);
	EXPECT_EQ(ov::ToUnderlyingType(OvtPayloadType::Required), 40);

	// A value the table has no entry for maps onto nothing, 0 included
	EXPECT_FALSE(ovt::FromOvtWire<OvtPayloadType>(0).has_value());
	EXPECT_FALSE(ovt::FromOvtWire<OvtPayloadType>(31).has_value());

	EXPECT_EQ(ovt::FromOvtWire<OvtPayloadType>(40), OvtPayloadType::Required);
}

// Items keep their on-wire order and each message carries the payload type it arrived on
TEST(OvtDepacketizerTest, MessagesCarryTheirPayloadType)
{
	OvtDepacketizer depacketizer;

	auto request = RawPacket(VERSION_BIT | MARKER_BIT, ov::ToUnderlyingType(OvtPayloadType::MessageRequest), 2, 2);
	auto response = RawPacket(VERSION_BIT | MARKER_BIT, ov::ToUnderlyingType(OvtPayloadType::MessageResponse), 3, 3);
	std::vector<uint8_t> media = RawPacket(VERSION_BIT | MARKER_BIT, ov::ToUnderlyingType(OvtPayloadType::MediaPacket), MEDIA_PACKET_HEADER_SIZE, MEDIA_PACKET_HEADER_SIZE);

	ASSERT_TRUE(depacketizer.AppendPacket(request.data(), request.size()));
	ASSERT_TRUE(depacketizer.AppendPacket(media.data(), media.size()));
	ASSERT_TRUE(depacketizer.AppendPacket(response.data(), response.size()));

	ASSERT_TRUE(depacketizer.IsAvailableMessage());
	auto first = depacketizer.PopMessage();
	ASSERT_TRUE(first.has_value());
	EXPECT_EQ(first->payload_type, OvtPayloadType::MessageRequest);
	EXPECT_EQ(first->data->GetLength(), 2u);

	EXPECT_FALSE(depacketizer.IsAvailableMessage());
	EXPECT_FALSE(depacketizer.PopMessage().has_value());
	ASSERT_TRUE(depacketizer.PopMediaPacket() != nullptr);

	ASSERT_TRUE(depacketizer.IsAvailableMessage());
	auto second = depacketizer.PopMessage();
	ASSERT_TRUE(second.has_value());
	EXPECT_EQ(second->payload_type, OvtPayloadType::MessageResponse);
	EXPECT_FALSE(depacketizer.IsAvailable());
}

// PT 40 is reassembled as a message and reports its payload type
TEST(OvtDepacketizerTest, RequiredIsAMessage)
{
	OvtDepacketizer depacketizer;
	auto required = RawPacket(VERSION_BIT | MARKER_BIT, ov::ToUnderlyingType(OvtPayloadType::Required), 2, 2);

	ASSERT_TRUE(depacketizer.AppendPacket(required.data(), required.size()));
	ASSERT_TRUE(depacketizer.IsAvailableMessage());
	auto message = depacketizer.PopMessage();
	ASSERT_TRUE(message.has_value());
	EXPECT_EQ(message->payload_type, OvtPayloadType::Required);
}

// Fragments of two different message types cannot share the reassembly buffer
TEST(OvtDepacketizerTest, InterleavedMessageTypesFail)
{
	OvtDepacketizer depacketizer;
	auto response_head = UnmarkedFragment(ov::ToUnderlyingType(OvtPayloadType::MessageResponse), 4);
	auto required = RawPacket(VERSION_BIT | MARKER_BIT, ov::ToUnderlyingType(OvtPayloadType::Required), 2, 2);

	ASSERT_TRUE(depacketizer.AppendPacket(response_head.data(), response_head.size()));
	EXPECT_FALSE(depacketizer.AppendPacket(required.data(), required.size()));
	EXPECT_FALSE(depacketizer.IsAvailable());
}

// A payload type this build does not know is consumed and dropped without failing the connection
TEST(OvtDepacketizerTest, UnknownPayloadTypeIsDroppedSilently)
{
	OvtDepacketizer depacketizer;
	auto unknown = RawPacket(VERSION_BIT | MARKER_BIT, 99, 4, 4);
	auto request = RawPacket(VERSION_BIT | MARKER_BIT, ov::ToUnderlyingType(OvtPayloadType::MessageRequest), 2, 2);

	std::vector<uint8_t> stream(unknown);
	stream.insert(stream.end(), request.begin(), request.end());

	ASSERT_TRUE(depacketizer.AppendPacket(stream.data(), stream.size()));
	auto message = depacketizer.PopMessage();
	ASSERT_TRUE(message.has_value());
	EXPECT_EQ(message->payload_type, OvtPayloadType::MessageRequest);
	EXPECT_FALSE(depacketizer.IsAvailable());
}

TEST(OvtDepacketizerTest, MessageReassemblyLimitIsEnforced)
{
	OvtDepacketizer depacketizer;
	const uint16_t fragment_size = OVT_DEFAULT_MAX_PACKET_SIZE - OVT_FIXED_HEADER_SIZE;
	auto fragment = UnmarkedFragment(ov::ToUnderlyingType(OvtPayloadType::MessageResponse), fragment_size);

	size_t appended = 0;
	bool rejected = false;
	while (appended <= MAX_MESSAGE_BUFFER_SIZE)
	{
		if (depacketizer.AppendPacket(fragment.data(), fragment.size()) == false)
		{
			rejected = true;
			break;
		}
		appended += fragment_size;
	}

	EXPECT_TRUE(rejected);
	EXPECT_GT(appended, MAX_MESSAGE_BUFFER_SIZE - fragment_size);
}

TEST(OvtDepacketizerTest, MediaReassemblyLimitIsEnforced)
{
	OvtDepacketizer depacketizer;
	const uint16_t fragment_size = OVT_DEFAULT_MAX_PACKET_SIZE - OVT_FIXED_HEADER_SIZE;
	auto fragment = UnmarkedFragment(ov::ToUnderlyingType(OvtPayloadType::MediaPacket), fragment_size);

	size_t appended = 0;
	bool rejected = false;
	while (appended <= MAX_MEDIA_PACKET_BUFFER_SIZE)
	{
		if (depacketizer.AppendPacket(fragment.data(), fragment.size()) == false)
		{
			rejected = true;
			break;
		}
		appended += fragment_size;
	}

	EXPECT_TRUE(rejected);
	EXPECT_GT(appended, MAX_MEDIA_PACKET_BUFFER_SIZE - fragment_size);
}

// ---- State machine details ----

namespace
{
	std::vector<uint8_t> MediaPayloadBytes(uint32_t data_size, uint32_t declared_size)
	{
		std::vector<uint8_t> payload(MEDIA_PACKET_HEADER_SIZE + data_size, 0);
		payload[28] = 0;   // Video
		payload[29] = 2;   // Key
		payload[30] = 1;   // H264_ANNEXB
		payload[31] = 3;   // NALU
		ByteWriter<uint32_t>::WriteBigEndian(&payload[32], declared_size);
		return payload;
	}

	std::vector<uint8_t> MediaPacketBytes(uint32_t data_size, uint32_t declared_size, bool marker = true)
	{
		auto payload = MediaPayloadBytes(data_size, declared_size);
		auto packet = RawPacket(static_cast<uint8_t>(VERSION_BIT | (marker ? MARKER_BIT : 0)), ov::ToUnderlyingType(OvtPayloadType::MediaPacket), static_cast<uint16_t>(payload.size()), 0);
		packet.insert(packet.end(), payload.begin(), payload.end());
		return packet;
	}
}  // namespace

// Feeding one byte at a time yields exactly one item, only once the last byte arrives
TEST(OvtDepacketizerTest, ByteByByteFeedReassemblesOnce)
{
	OvtDepacketizer depacketizer;
	auto packet = MediaPacketBytes(5, 5);

	for (size_t i = 0; i + 1 < packet.size(); i++)
	{
		ASSERT_TRUE(depacketizer.AppendPacket(&packet[i], 1)) << i;
		EXPECT_FALSE(depacketizer.IsAvailable()) << i;
	}
	ASSERT_TRUE(depacketizer.AppendPacket(&packet[packet.size() - 1], 1));
	EXPECT_TRUE(depacketizer.IsAvailableMediaPacket());
	ASSERT_TRUE(depacketizer.PopMediaPacket() != nullptr);
	EXPECT_FALSE(depacketizer.IsAvailable());
}

// Several complete packets in one append all come out, in order
TEST(OvtDepacketizerTest, SeveralPacketsInOneAppend)
{
	OvtDepacketizer depacketizer;
	std::vector<uint8_t> stream;
	for (int i = 0; i < 3; i++)
	{
		auto packet = RawPacket(VERSION_BIT | MARKER_BIT, ov::ToUnderlyingType(OvtPayloadType::MessageResponse), 1, 1);
		packet.back() = static_cast<uint8_t>(i);
		stream.insert(stream.end(), packet.begin(), packet.end());
	}

	ASSERT_TRUE(depacketizer.AppendPacket(stream.data(), stream.size()));
	for (int i = 0; i < 3; i++)
	{
		auto message = depacketizer.PopMessage();
		ASSERT_TRUE(message.has_value()) << i;
		EXPECT_EQ(message->data->GetDataAs<uint8_t>()[0], static_cast<uint8_t>(i));
	}
	EXPECT_FALSE(depacketizer.IsAvailable());
}

// A declared data size that does not match the reassembled length is a broken packet
TEST(OvtDepacketizerTest, DataSizeMismatchFails)
{
	OvtDepacketizer too_small;
	auto packet = MediaPacketBytes(5, 4);
	EXPECT_FALSE(too_small.AppendPacket(packet.data(), packet.size()));

	OvtDepacketizer too_large;
	packet = MediaPacketBytes(5, 6);
	EXPECT_FALSE(too_large.AppendPacket(packet.data(), packet.size()));
}

// A media payload shorter than the 36-byte header is invalid
TEST(OvtDepacketizerTest, MediaShorterThanHeaderFails)
{
	OvtDepacketizer depacketizer;
	auto packet = RawPacket(VERSION_BIT | MARKER_BIT, ov::ToUnderlyingType(OvtPayloadType::MediaPacket), 10, 10);
	EXPECT_FALSE(depacketizer.AppendPacket(packet.data(), packet.size()));
}

// A message whose marker arrives with nothing accumulated is invalid
TEST(OvtDepacketizerTest, EmptyMessageFails)
{
	OvtDepacketizer depacketizer;
	auto packet = RawPacket(VERSION_BIT | MARKER_BIT, ov::ToUnderlyingType(OvtPayloadType::MessageRequest), 0, 0);
	EXPECT_FALSE(depacketizer.AppendPacket(packet.data(), packet.size()));
}

// A media fragment group split over two OVT packets is joined and the data size is checked over the whole
TEST(OvtDepacketizerTest, TwoFragmentMediaGroup)
{
	OvtDepacketizer depacketizer;
	auto payload = MediaPayloadBytes(8, 8);
	for (size_t i = 0; i < 8; i++)
	{
		payload[MEDIA_PACKET_HEADER_SIZE + i] = static_cast<uint8_t>(0x10 + i);
	}

	// First 40 bytes without marker, the remaining 4 with marker
	auto first = RawPacket(VERSION_BIT, ov::ToUnderlyingType(OvtPayloadType::MediaPacket), 40, 0);
	first.insert(first.end(), payload.begin(), payload.begin() + 40);
	auto last = RawPacket(VERSION_BIT | MARKER_BIT, ov::ToUnderlyingType(OvtPayloadType::MediaPacket), 4, 0);
	last.insert(last.end(), payload.begin() + 40, payload.end());

	ASSERT_TRUE(depacketizer.AppendPacket(first.data(), first.size()));
	EXPECT_FALSE(depacketizer.IsAvailable());
	ASSERT_TRUE(depacketizer.AppendPacket(last.data(), last.size()));
	auto media = depacketizer.PopMediaPacket();
	ASSERT_TRUE(media != nullptr);
	ASSERT_EQ(media->GetDataLength(), 8u);
	EXPECT_EQ(media->GetData()->GetDataAs<uint8_t>()[7], 0x17);
	EXPECT_TRUE(media->IsKeyFrame());
}

// Popping the wrong kind is a guarded no-op and never desynchronizes the queue
TEST(OvtDepacketizerTest, PopOfTheWrongKindIsANoOp)
{
	OvtDepacketizer depacketizer;
	auto media = MediaPacketBytes(1, 1);
	auto message = RawPacket(VERSION_BIT | MARKER_BIT, ov::ToUnderlyingType(OvtPayloadType::MessageResponse), 1, 1);

	ASSERT_TRUE(depacketizer.AppendPacket(media.data(), media.size()));
	ASSERT_TRUE(depacketizer.AppendPacket(message.data(), message.size()));

	EXPECT_FALSE(depacketizer.PopMessage().has_value());  // front is media
	EXPECT_TRUE(depacketizer.IsAvailableMediaPacket());
	ASSERT_TRUE(depacketizer.PopMediaPacket() != nullptr);
	EXPECT_EQ(depacketizer.PopMediaPacket(), nullptr);  // front is a message now
	EXPECT_TRUE(depacketizer.PopMessage().has_value());
	EXPECT_FALSE(depacketizer.IsAvailable());
}

// A garbage byte after a valid packet fails the connection at the next header
TEST(OvtDepacketizerTest, GarbageAfterValidPacketFailsOnNextHeader)
{
	OvtDepacketizer depacketizer;
	auto good = RawPacket(VERSION_BIT | MARKER_BIT, ov::ToUnderlyingType(OvtPayloadType::MessageRequest), 1, 1);
	ASSERT_TRUE(depacketizer.AppendPacket(good.data(), good.size()));
	ASSERT_TRUE(depacketizer.PopMessage().has_value());

	std::vector<uint8_t> garbage(OVT_FIXED_HEADER_SIZE, 0xFF);
	EXPECT_FALSE(depacketizer.AppendPacket(garbage.data(), garbage.size()));
}

// Fewer than 18 bytes of a next header are simply held
TEST(OvtDepacketizerTest, PartialHeaderIsHeld)
{
	OvtDepacketizer depacketizer;
	auto packet = RawPacket(VERSION_BIT | MARKER_BIT, ov::ToUnderlyingType(OvtPayloadType::MessageRequest), 1, 1);
	ASSERT_TRUE(depacketizer.AppendPacket(packet.data(), 10));
	EXPECT_FALSE(depacketizer.IsAvailable());
	ASSERT_TRUE(depacketizer.AppendPacket(&packet[10], packet.size() - 10));
	EXPECT_TRUE(depacketizer.IsAvailableMessage());
}

// Header bytes of the media packet are read from the exact offsets of the 36-byte layout
TEST(OvtDepacketizerTest, MediaHeaderOffsets)
{
	OvtDepacketizer depacketizer;
	std::vector<uint8_t> payload(MEDIA_PACKET_HEADER_SIZE + 2, 0);
	ByteWriter<uint32_t>::WriteBigEndian(&payload[0], 0x01020304);
	ByteWriter<uint64_t>::WriteBigEndian(&payload[4], 0x1122334455667788ULL);
	ByteWriter<uint64_t>::WriteBigEndian(&payload[12], 0x0102030405060708ULL);
	ByteWriter<uint64_t>::WriteBigEndian(&payload[20], 3003);
	payload[28] = 1;	 // Audio
	payload[29] = 1;	 // NoFlag
	payload[30] = 12;	 // OPUS
	payload[31] = 1;	 // RAW
	ByteWriter<uint32_t>::WriteBigEndian(&payload[32], 2);
	payload[36] = 0xAB;
	payload[37] = 0xCD;

	auto packet = RawPacket(VERSION_BIT | MARKER_BIT, ov::ToUnderlyingType(OvtPayloadType::MediaPacket), static_cast<uint16_t>(payload.size()), 0);
	packet.insert(packet.end(), payload.begin(), payload.end());

	ASSERT_TRUE(depacketizer.AppendPacket(packet.data(), packet.size()));
	auto media = depacketizer.PopMediaPacket();
	ASSERT_TRUE(media != nullptr);
	EXPECT_EQ(media->GetTrackId(), 0x01020304u);
	EXPECT_EQ(media->GetPts(), static_cast<int64_t>(0x1122334455667788ULL));
	EXPECT_EQ(media->GetDts(), static_cast<int64_t>(0x0102030405060708ULL));
	EXPECT_EQ(media->GetDuration(), 3003);
	EXPECT_EQ(media->GetMediaType(), cmn::MediaType::Audio);
	EXPECT_EQ(media->GetFlag(), MediaPacketFlag::NoFlag);
	EXPECT_EQ(media->GetBitstreamFormat(), cmn::BitstreamFormat::OPUS);
	EXPECT_EQ(media->GetPacketType(), cmn::PacketType::RAW);
	ASSERT_EQ(media->GetDataLength(), 2u);
	EXPECT_EQ(media->GetData()->GetDataAs<uint8_t>()[1], 0xCD);
}

// After a rejected message the depacketizer's message buffer is empty, so a following valid message is intact
TEST(OvtDepacketizerTest, MessageBufferIsClearedOnFailure)
{
	OvtDepacketizer depacketizer;
	const uint16_t fragment_size = OVT_DEFAULT_MAX_PACKET_SIZE - OVT_FIXED_HEADER_SIZE;
	auto fragment = UnmarkedFragment(ov::ToUnderlyingType(OvtPayloadType::MessageResponse), fragment_size);
	size_t appended = 0;
	while (depacketizer.AppendPacket(fragment.data(), fragment.size()))
	{
		appended += fragment_size;
		ASSERT_LT(appended, MAX_MESSAGE_BUFFER_SIZE + 2 * fragment_size);
	}

	auto good = RawPacket(VERSION_BIT | MARKER_BIT, ov::ToUnderlyingType(OvtPayloadType::MessageResponse), 3, 3);
	ASSERT_TRUE(depacketizer.AppendPacket(good.data(), good.size()));
	auto message = depacketizer.PopMessage();
	ASSERT_TRUE(message.has_value());
	EXPECT_EQ(message->data->GetLength(), 3u);
}

// A connection gets its own depacketizer. The instance is replaced rather than cleared, so a
// half-received packet from the previous connection can never be read as the head of the next one,
// and a thread still working on the previous instance is not left holding a destroyed queue.
TEST(OvtDepacketizerTest, AHalfReceivedPacketDoesNotCrossAConnection)
{
	auto payload = std::vector<uint8_t>(100, 0xAB);
	auto packet = RawPacket(VERSION_BIT | MARKER_BIT, ov::ToUnderlyingType(OvtPayloadType::MessageRequest), payload.size(), payload.size());
	std::copy(payload.begin(), payload.end(), packet.begin() + OVT_FIXED_HEADER_SIZE);

	// The previous connection stopped in the middle of a packet
	auto previous = std::make_shared<OvtDepacketizer>();
	auto half = packet.size() / 2;
	ASSERT_TRUE(previous->AppendPacket(packet.data(), half));
	EXPECT_FALSE(previous->IsAvailable());

	// The next connection starts on its own instance and sees only what it received.
	// The tail is not a header, so it is refused outright and the connection is dropped:
	// the bytes are never read as the head of the next packet.
	auto current = std::make_shared<OvtDepacketizer>();
	EXPECT_FALSE(current->AppendPacket(packet.data() + half, packet.size() - half));
	EXPECT_FALSE(current->IsAvailable()) << "the previous connection's head must not complete this packet";

	// A whole packet on an instance that never saw the tail still works
	auto fresh = std::make_shared<OvtDepacketizer>();
	ASSERT_TRUE(fresh->AppendPacket(packet.data(), packet.size()));
	EXPECT_TRUE(fresh->IsAvailableMessage());

	// The old instance is untouched by any of that and is still holding its own half
	EXPECT_FALSE(previous->IsAvailable());
	ASSERT_TRUE(previous->AppendPacket(packet.data() + half, packet.size() - half));
	EXPECT_TRUE(previous->IsAvailableMessage()) << "the previous instance completes its own packet";
}
