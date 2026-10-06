//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2026 OvenMediaLabs. All rights reserved.
//
//==============================================================================
#include "ovt_packetizer.h"

#include <gtest/gtest.h>

#include <cstring>
#include <vector>

#include "ovt_depacketizer.h"

// Fragmentation rules of the packetizer, and that the depacketizer undoes them exactly

namespace
{
	constexpr size_t MAX_PAYLOAD = OVT_DEFAULT_MAX_PACKET_SIZE - OVT_FIXED_HEADER_SIZE;

	std::vector<std::shared_ptr<OvtPacket>> Drain(OvtPacketizer &packetizer)
	{
		std::vector<std::shared_ptr<OvtPacket>> packets;
		while (packetizer.IsAvailablePackets())
		{
			packets.push_back(packetizer.PopPacket());
		}
		return packets;
	}

	std::shared_ptr<ov::Data> Pattern(size_t length)
	{
		std::vector<uint8_t> bytes(length);
		for (size_t i = 0; i < length; i++)
		{
			bytes[i] = static_cast<uint8_t>((i * 7 + 3) & 0xFF);
		}
		return std::make_shared<ov::Data>(bytes.data(), bytes.size());
	}
}  // namespace

TEST(OvtPacketizerTest, SmallMessageIsOnePacketWithMarker)
{
	OvtPacketizer packetizer;
	auto message = Pattern(100);

	ASSERT_TRUE(packetizer.PacketizeMessage(OvtPayloadType::MessageRequest, 12345, message));
	auto packets = Drain(packetizer);

	ASSERT_EQ(packets.size(), 1u);
	EXPECT_TRUE(packets[0]->Marker());
	EXPECT_EQ(packets[0]->PayloadType(), 10);
	EXPECT_EQ(packets[0]->SequenceNumber(), 0);
	EXPECT_EQ(packets[0]->Timestamp(), 12345u);
	EXPECT_EQ(packets[0]->SessionId(), 0u);
	EXPECT_EQ(packets[0]->PayloadLength(), 100);
	EXPECT_EQ(memcmp(packets[0]->Payload(), message->GetData(), 100), 0);
}

// Messages split at the payload maximum; only the last fragment has the marker and sequence numbers restart at 0
TEST(OvtPacketizerTest, LargeMessageIsFragmented)
{
	OvtPacketizer packetizer;
	auto message = Pattern(MAX_PAYLOAD * 2 + 17);

	ASSERT_TRUE(packetizer.PacketizeMessage(OvtPayloadType::MessageResponse, 1, message));
	auto packets = Drain(packetizer);

	ASSERT_EQ(packets.size(), 3u);
	EXPECT_EQ(packets[0]->PayloadLength(), MAX_PAYLOAD);
	EXPECT_EQ(packets[1]->PayloadLength(), MAX_PAYLOAD);
	EXPECT_EQ(packets[2]->PayloadLength(), 17);
	EXPECT_FALSE(packets[0]->Marker());
	EXPECT_FALSE(packets[1]->Marker());
	EXPECT_TRUE(packets[2]->Marker());
	EXPECT_EQ(packets[0]->SequenceNumber(), 0);
	EXPECT_EQ(packets[1]->SequenceNumber(), 1);
	EXPECT_EQ(packets[2]->SequenceNumber(), 2);

	// A second message starts its own sequence at 0
	ASSERT_TRUE(packetizer.PacketizeMessage(OvtPayloadType::MessageResponse, 2, Pattern(10)));
	auto second = Drain(packetizer);
	ASSERT_EQ(second.size(), 1u);
	EXPECT_EQ(second[0]->SequenceNumber(), 0);
}

// A message of exactly the maximum is one packet, one byte more is two
TEST(OvtPacketizerTest, MessageBoundaryAtMaxPayload)
{
	OvtPacketizer packetizer;
	ASSERT_TRUE(packetizer.PacketizeMessage(OvtPayloadType::MessageRequest, 0, Pattern(MAX_PAYLOAD)));
	EXPECT_EQ(Drain(packetizer).size(), 1u);

	ASSERT_TRUE(packetizer.PacketizeMessage(OvtPayloadType::MessageRequest, 0, Pattern(MAX_PAYLOAD + 1)));
	auto packets = Drain(packetizer);
	ASSERT_EQ(packets.size(), 2u);
	EXPECT_EQ(packets[1]->PayloadLength(), 1);
}

// Media packets share one running sequence across calls; messages do not disturb it
TEST(OvtPacketizerTest, MediaSequenceRunsAcrossPackets)
{
	OvtPacketizer packetizer;
	const uint8_t data[4] = {1, 2, 3, 4};
	auto media			  = std::make_shared<MediaPacket>(cmn::MediaType::Video, 1, data, sizeof(data), 0, 0, 0, MediaPacketFlag::Key, cmn::BitstreamFormat::H264_ANNEXB, cmn::PacketType::NALU);

	ASSERT_TRUE(packetizer.PacketizeMediaPacket(100, media));
	ASSERT_TRUE(packetizer.PacketizeMessage(OvtPayloadType::MessageResponse, 0, Pattern(5)));
	ASSERT_TRUE(packetizer.PacketizeMediaPacket(200, media));
	auto packets = Drain(packetizer);

	ASSERT_EQ(packets.size(), 3u);
	EXPECT_EQ(packets[0]->SequenceNumber(), 0);
	EXPECT_EQ(packets[0]->PayloadType(), 30);
	EXPECT_EQ(packets[0]->Timestamp(), 100u);
	EXPECT_EQ(packets[2]->SequenceNumber(), 1);
	EXPECT_EQ(packets[2]->Timestamp(), 200u);
	EXPECT_EQ(packets[0]->PayloadLength(), MEDIA_PACKET_HEADER_SIZE + 4);
	EXPECT_TRUE(packets[0]->Marker());
}

// A media packet larger than one OVT packet is fragmented and reassembled byte for byte, whatever the chunking
TEST(OvtPacketizerTest, LargeMediaRoundTrip)
{
	OvtPacketizer packetizer;
	auto payload = Pattern(MAX_PAYLOAD * 3 + 1000);
	auto media	 = std::make_shared<MediaPacket>(cmn::MediaType::Audio, 77, payload, 1000, 900, 20, MediaPacketFlag::NoFlag, cmn::BitstreamFormat::AAC_ADTS, cmn::PacketType::RAW);

	ASSERT_TRUE(packetizer.PacketizeMediaPacket(0, media));
	auto packets = Drain(packetizer);
	ASSERT_EQ(packets.size(), 4u);
	for (size_t i = 0; i < packets.size(); i++)
	{
		EXPECT_EQ(packets[i]->Marker(), i == packets.size() - 1) << i;
		EXPECT_EQ(packets[i]->SequenceNumber(), i) << i;
	}

	// Concatenate and feed back in odd-sized chunks
	std::vector<uint8_t> wire;
	for (const auto &packet : packets)
	{
		wire.insert(wire.end(), packet->GetBuffer(), packet->GetBuffer() + packet->GetDataLength());
	}

	OvtDepacketizer depacketizer;
	size_t offset			   = 0;
	const size_t chunk_sizes[] = {1, 17, 5000, 33333, 7};
	size_t k				   = 0;
	while (offset < wire.size())
	{
		auto n = std::min(chunk_sizes[k++ % 5], wire.size() - offset);
		ASSERT_TRUE(depacketizer.AppendPacket(&wire[offset], n));
		offset += n;
	}

	ASSERT_TRUE(depacketizer.IsAvailableMediaPacket());
	auto received = depacketizer.PopMediaPacket();
	ASSERT_TRUE(received != nullptr);
	EXPECT_EQ(received->GetTrackId(), 77u);
	EXPECT_EQ(received->GetPts(), 1000);
	EXPECT_EQ(received->GetDts(), 900);
	EXPECT_EQ(received->GetDuration(), 20);
	EXPECT_EQ(received->GetFlag(), MediaPacketFlag::NoFlag);
	EXPECT_EQ(received->GetBitstreamFormat(), cmn::BitstreamFormat::AAC_ADTS);
	EXPECT_EQ(received->GetPacketType(), cmn::PacketType::RAW);
	ASSERT_EQ(received->GetDataLength(), payload->GetLength());
	EXPECT_EQ(memcmp(received->GetData()->GetData(), payload->GetData(), payload->GetLength()), 0);
	EXPECT_FALSE(depacketizer.IsAvailable());
}

// A packet without data still carries the 36-byte header with data size 0
TEST(OvtPacketizerTest, MediaWithoutDataIsHeaderOnly)
{
	OvtPacketizer packetizer;
	auto media = std::make_shared<MediaPacket>(cmn::MediaType::Data, 5, nullptr, 0, 0, 0, MediaPacketFlag::NoFlag, cmn::BitstreamFormat::ID3v2, cmn::PacketType::EVENT);

	ASSERT_TRUE(packetizer.PacketizeMediaPacket(0, media));
	auto packets = Drain(packetizer);
	ASSERT_EQ(packets.size(), 1u);
	EXPECT_EQ(packets[0]->PayloadLength(), MEDIA_PACKET_HEADER_SIZE);

	OvtDepacketizer depacketizer;
	ASSERT_TRUE(depacketizer.AppendPacket(packets[0]->GetData()));
	auto received = depacketizer.PopMediaPacket();
	ASSERT_TRUE(received != nullptr);
	EXPECT_EQ(received->GetDataLength(), 0u);
	EXPECT_EQ(received->GetMediaType(), cmn::MediaType::Data);
}

// A callback sink receives every fragment in order and can stop the packetizer by returning false
namespace
{
	class Sink : public OvtPacketizerInterface
	{
	public:
		bool OnOvtPacketized(std::shared_ptr<OvtPacket> &packet) override
		{
			sequences.push_back(packet->SequenceNumber());
			return accept;
		}

		std::vector<uint16_t> sequences;
		bool accept = true;
	};
}  // namespace

TEST(OvtPacketizerTest, CallbackSinkReceivesFragmentsAndCanAbort)
{
	auto sink = std::make_shared<Sink>();
	OvtPacketizer packetizer(sink);

	ASSERT_TRUE(packetizer.PacketizeMessage(OvtPayloadType::MessageResponse, 0, Pattern(MAX_PAYLOAD + 1)));
	EXPECT_EQ(sink->sequences, (std::vector<uint16_t>{0, 1}));
	EXPECT_FALSE(packetizer.IsAvailablePackets());

	sink->accept = false;
	sink->sequences.clear();
	EXPECT_FALSE(packetizer.PacketizeMessage(OvtPayloadType::MessageResponse, 0, Pattern(MAX_PAYLOAD + 1)));
	EXPECT_EQ(sink->sequences.size(), 1u);
}

// A track snapshot for a stream with many tracks does not fit in one packet, and neither does a
// large describe. Every fragment of such a message must survive the round trip, because the sender
// writes them with one Send() and the receiver appends consecutive fragments into one buffer.
TEST(OvtPacketizerTest, AMessageLargerThanOnePacketSurvivesTheRoundTrip)
{
	// Well past OVT_DEFAULT_MAX_PACKET_SIZE so several fragments are needed
	std::string big(200 * 1024, 'x');
	for (size_t i = 0; i < big.size(); i += 997)
	{
		big[i] = static_cast<char>('A' + (i % 26));
	}
	auto payload = std::make_shared<ov::Data>(big.data(), big.size());

	OvtPacketizer packetizer;
	ASSERT_TRUE(packetizer.PacketizeMessage(OvtPayloadType::MessageResponse, 12345, payload));

	// The sender concatenates every fragment into one buffer before writing it
	auto wire		 = std::make_shared<ov::Data>();
	size_t fragments = 0;
	while (packetizer.IsAvailablePackets())
	{
		auto packet = packetizer.PopPacket();
		ASSERT_NE(packet, nullptr);
		wire->Append(packet->GetData());
		fragments++;
	}
	EXPECT_GT(fragments, 1u) << "the payload must not fit in a single packet, or this proves nothing";

	OvtDepacketizer depacketizer;
	ASSERT_TRUE(depacketizer.AppendPacket(wire));
	ASSERT_TRUE(depacketizer.IsAvailableMessage());

	auto message = depacketizer.PopMessage();
	ASSERT_TRUE(message.has_value());
	EXPECT_EQ(message->payload_type, OvtPayloadType::MessageResponse);
	ASSERT_EQ(message->data->GetLength(), big.size());
	EXPECT_EQ(std::memcmp(message->data->GetData(), big.data(), big.size()), 0);
	EXPECT_FALSE(depacketizer.IsAvailable());
}

// Fragments arriving one byte at a time must reassemble the same way: a receiver has no say in
// how a write is split across reads.
TEST(OvtPacketizerTest, AFragmentedMessageReassemblesByteByByte)
{
	std::string big(70 * 1024, 'q');
	auto payload = std::make_shared<ov::Data>(big.data(), big.size());

	OvtPacketizer packetizer;
	ASSERT_TRUE(packetizer.PacketizeMessage(OvtPayloadType::MessageResponse, 1, payload));

	auto wire = std::make_shared<ov::Data>();
	while (packetizer.IsAvailablePackets())
	{
		wire->Append(packetizer.PopPacket()->GetData());
	}

	OvtDepacketizer depacketizer;
	auto bytes = wire->GetDataAs<uint8_t>();
	for (size_t i = 0; i < wire->GetLength(); i++)
	{
		ASSERT_TRUE(depacketizer.AppendPacket(&bytes[i], 1)) << "stopped at byte " << i;
	}

	ASSERT_TRUE(depacketizer.IsAvailableMessage());
	auto message = depacketizer.PopMessage();
	ASSERT_TRUE(message.has_value());
	EXPECT_EQ(message->data->GetLength(), big.size());
}
