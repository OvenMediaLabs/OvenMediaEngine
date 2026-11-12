//==============================================================================
//
//  OvenMediaEngine
//
//  Copyright (c) 2026 OvenMediaLabs. All rights reserved.
//
//==============================================================================
#include "ulpfec_generator.h"

#include <gtest/gtest.h>

#include <map>
#include <set>

#include "base/ovlibrary/byte_io.h"
#include "rtp_packetizer.h"

namespace
{
	constexpr uint8_t kMediaPayloadType	 = 96;
	constexpr uint8_t kRedPayloadType	 = 120;
	constexpr uint8_t kUlpfecPayloadType = 122;
	constexpr uint32_t kSsrc			 = 0x11223344;
	constexpr uint32_t kTrackId			 = 7;

	class CollectingSession : public RtpPacketizerInterface
	{
	public:
		bool OnRtpPacketized(std::shared_ptr<RtpPacket> packet) override
		{
			packets.push_back(packet);
			return true;
		}

		std::vector<std::shared_ptr<RtpPacket>> packets;
	};

	struct FecPacket
	{
		uint16_t sequence_number = 0;
		std::vector<uint8_t> data;	// after the RED header
		std::shared_ptr<RtpPacket> packet;
	};

	struct RedStream
	{
		// Media packets as a receiver rebuilds them from RED, by sequence number
		std::map<uint16_t, std::vector<uint8_t>> media;
		std::vector<FecPacket> fec;
	};

	// Splits the RED packets the packetizer sent the way a receiver does, renumbering them by `renumber`
	RedStream SplitRed(const std::vector<std::shared_ptr<RtpPacket>> &packets, uint16_t renumber)
	{
		RedStream stream;

		for (const auto &packet : packets)
		{
			if (packet->PayloadType() != kRedPayloadType)
			{
				continue;
			}

			const uint8_t *header = packet->Buffer();
			const size_t header_size = packet->HeadersSize();
			const uint8_t *payload = packet->Payload();
			const size_t payload_size = packet->PayloadSize();
			const uint16_t sequence_number = packet->SequenceNumber() + renumber;
			const uint8_t block_payload_type = payload[0] & 0x7F;

			if (block_payload_type == kUlpfecPayloadType)
			{
				// What a sender that renumbers the stream does to the FEC header
				RtpPacket renumbered(*packet);
				EXPECT_TRUE(UlpfecGenerator::ShiftSequenceNumberBase(&renumbered, renumber));

				FecPacket fec_packet;
				fec_packet.sequence_number = sequence_number;
				fec_packet.data.assign(renumbered.Payload() + 1, renumbered.Payload() + renumbered.PayloadSize());
				fec_packet.packet = packet;

				stream.fec.push_back(fec_packet);
				continue;
			}

			std::vector<uint8_t> media(header, header + header_size);
			media[1] = (media[1] & 0x80) | block_payload_type;
			ByteWriter<uint16_t>::WriteBigEndian(&media[2], sequence_number);
			media.insert(media.end(), payload + 1, payload + payload_size);

			stream.media[sequence_number] = media;
		}

		return stream;
	}

	std::vector<uint16_t> ProtectedSequenceNumbers(const std::vector<uint8_t> &fec_data)
	{
		const size_t mask_size = ((fec_data[0] & 0x40) != 0) ? 6 : 2;
		const uint16_t base = ByteReader<uint16_t>::ReadBigEndian(&fec_data[2]);

		std::vector<uint16_t> sequence_numbers;
		for (size_t bit = 0; bit < mask_size * 8; bit++)
		{
			if ((fec_data[12 + (bit / 8)] & (0x80 >> (bit % 8))) != 0)
			{
				sequence_numbers.push_back(static_cast<uint16_t>(base + bit));
			}
		}

		return sequence_numbers;
	}

	// RFC 5109 recovery of `missing` from one FEC packet and the other packets it protects
	std::vector<uint8_t> Recover(const std::vector<uint8_t> &fec_data, uint32_t ssrc,
								 const std::map<uint16_t, std::vector<uint8_t>> &media, uint16_t missing)
	{
		const size_t mask_size = ((fec_data[0] & 0x40) != 0) ? 6 : 2;
		const size_t fec_header_size = 12 + mask_size;
		const size_t protection_length = ByteReader<uint16_t>::ReadBigEndian(&fec_data[10]);

		uint8_t first_byte = fec_data[0];
		uint8_t second_byte = fec_data[1];
		uint32_t timestamp = ByteReader<uint32_t>::ReadBigEndian(&fec_data[4]);
		uint16_t length = ByteReader<uint16_t>::ReadBigEndian(&fec_data[8]);
		std::vector<uint8_t> body(fec_data.begin() + fec_header_size, fec_data.begin() + fec_header_size + protection_length);

		for (auto sequence_number : ProtectedSequenceNumbers(fec_data))
		{
			if (sequence_number == missing)
			{
				continue;
			}

			const auto &packet = media.at(sequence_number);
			first_byte ^= packet[0];
			second_byte ^= packet[1];
			timestamp ^= ByteReader<uint32_t>::ReadBigEndian(&packet[4]);
			length ^= static_cast<uint16_t>(packet.size() - FIXED_HEADER_SIZE);

			for (size_t index = FIXED_HEADER_SIZE; index < packet.size(); index++)
			{
				body[index - FIXED_HEADER_SIZE] ^= packet[index];
			}
		}

		std::vector<uint8_t> recovered(FIXED_HEADER_SIZE + length);
		recovered[0] = 0x80 | (first_byte & 0x3F);
		recovered[1] = second_byte;
		ByteWriter<uint16_t>::WriteBigEndian(&recovered[2], missing);
		ByteWriter<uint32_t>::WriteBigEndian(&recovered[4], timestamp);
		ByteWriter<uint32_t>::WriteBigEndian(&recovered[8], ssrc);
		std::copy(body.begin(), body.begin() + length, recovered.begin() + FIXED_HEADER_SIZE);

		return recovered;
	}

	std::shared_ptr<CollectingSession> PacketizeH264Frames(const std::vector<std::pair<FrameType, size_t>> &frames)
	{
		auto session = std::make_shared<CollectingSession>();
		RtpPacketizer packetizer(session);

		packetizer.SetCodec(cmn::MediaCodecId::H264);
		packetizer.SetTrackId(kTrackId);
		packetizer.SetPayloadType(kMediaPayloadType);
		packetizer.SetSSRC(kSsrc);
		packetizer.EnableTransportCc(0);
		packetizer.EnableAbsSendTime();
		packetizer.SetUlpfec(kRedPayloadType, kUlpfecPayloadType);

		uint32_t timestamp = 90000;
		uint8_t fill = 1;

		for (const auto &[frame_type, frame_size] : frames)
		{
			std::vector<uint8_t> frame(frame_size);
			for (auto &byte : frame)
			{
				byte = fill;
				fill = static_cast<uint8_t>((fill * 31) + 7);
			}
			frame[0] = (frame_type == FrameType::VideoFrameKey) ? 0x65 : 0x41;

			FragmentationHeader fragmentation;
			fragmentation.AddFragment(0, frame.size());

			RTPVideoHeader video_header = {};
			video_header.codec = cmn::MediaCodecId::H264;
			video_header.codec_header.h26X.packetization_mode = H26XPacketizationMode::NonInterleaved;

			EXPECT_TRUE(packetizer.Packetize(frame_type, timestamp, timestamp * 10, frame.data(), frame.size(), &fragmentation, &video_header));
			timestamp += 3000;
		}

		return session;
	}

	// What a receiver rebuilds when `lost` are missing: an FEC packet missing one packet rebuilds it, until none does
	std::map<uint16_t, std::vector<uint8_t>> Rebuild(const RedStream &stream, const std::set<uint16_t> &lost)
	{
		auto media = stream.media;
		for (auto sequence_number : lost)
		{
			media.erase(sequence_number);
		}

		for (bool progress = true; progress;)
		{
			progress = false;

			for (const auto &fec_packet : stream.fec)
			{
				std::vector<uint16_t> missing;
				for (auto sequence_number : ProtectedSequenceNumbers(fec_packet.data))
				{
					if (media.count(sequence_number) == 0)
					{
						missing.push_back(sequence_number);
					}
				}

				if (missing.size() == 1)
				{
					media[missing[0]] = Recover(fec_packet.data, kSsrc, media, missing[0]);
					progress = true;
				}
			}
		}

		return media;
	}

	void ExpectRebuilt(const RedStream &stream, const std::set<uint16_t> &lost)
	{
		auto media = Rebuild(stream, lost);

		for (auto sequence_number : lost)
		{
			ASSERT_EQ(media.count(sequence_number), 1u) << "packet " << sequence_number << " did not come back";
			EXPECT_EQ(media.at(sequence_number), stream.media.at(sequence_number)) << "packet " << sequence_number;
		}
	}

	// Every packet is protected, and one lost packet comes back exactly
	void ExpectEveryPacketRecovers(const RedStream &stream)
	{
		std::set<uint16_t> covered;

		for (const auto &fec_packet : stream.fec)
		{
			for (auto sequence_number : ProtectedSequenceNumbers(fec_packet.data))
			{
				ASSERT_EQ(stream.media.count(sequence_number), 1u) << "FEC " << fec_packet.sequence_number << " names " << sequence_number;
				covered.insert(sequence_number);
			}
		}

		EXPECT_EQ(covered.size(), stream.media.size()) << "some media packets are in no FEC packet";

		for (const auto &[sequence_number, packet] : stream.media)
		{
			ExpectRebuilt(stream, {sequence_number});
		}
	}
}  // namespace

TEST(UlpfecGenerator, ALostPacketIsRebuiltExactlyHeaderExtensionsIncluded)
{
	auto session = PacketizeH264Frames({{FrameType::VideoFrameKey, 20000}, {FrameType::VideoFrameDelta, 3000}, {FrameType::VideoFrameDelta, 600}});
	auto stream = SplitRed(session->packets, 0);

	ASSERT_FALSE(stream.fec.empty());
	ASSERT_FALSE(stream.media.empty());
	EXPECT_NE(stream.media.begin()->second[0] & 0x10, 0) << "the media packets are expected to carry header extensions";

	ExpectEveryPacketRecovers(stream);
}

TEST(UlpfecGenerator, TheSequenceNumberBaseFollowsARenumberedStream)
{
	auto session = PacketizeH264Frames({{FrameType::VideoFrameKey, 20000}, {FrameType::VideoFrameDelta, 3000}});

	// Across the 16-bit wrap as well
	for (uint16_t renumber : {static_cast<uint16_t>(1234), static_cast<uint16_t>(0xFFF0)})
	{
		ExpectEveryPacketRecovers(SplitRed(session->packets, renumber));
	}
}

TEST(UlpfecGenerator, AnyTwoLostPacketsOfAFrameComeBack)
{
	// About 40 packets, protected by 6 FEC packets that have more than 40 different sets to give out
	auto session = PacketizeH264Frames({{FrameType::VideoFrameKey, 52000}});
	auto stream = SplitRed(session->packets, 0);

	ASSERT_GT(stream.media.size(), 35u);
	ASSERT_LE(stream.media.size(), 48u);

	for (auto first = stream.media.begin(); first != stream.media.end(); first++)
	{
		for (auto second = std::next(first); second != stream.media.end(); second++)
		{
			ExpectRebuilt(stream, {first->first, second->first});
		}
	}
}

TEST(UlpfecGenerator, TwoNeighboringLostPacketsComeBack)
{
	// Fewer FEC packets than it takes to give every packet its own set, so sets repeat but not on neighbors
	auto session = PacketizeH264Frames({{FrameType::VideoFrameDelta, 12000}});
	auto stream = SplitRed(session->packets, 0);

	ASSERT_GT(stream.media.size(), 7u);

	for (auto first = stream.media.begin(); std::next(first) != stream.media.end(); first++)
	{
		ExpectRebuilt(stream, {first->first, std::next(first)->first});
	}
}

TEST(UlpfecGenerator, AFrameLongerThanAMaskIsSplitIntoBlocks)
{
	// More than 48 packets, so one mask cannot reach them all; the frame still gets one FEC packet per seven
	for (size_t frame_size : {70000, 120000, 300000})
	{
		auto session = PacketizeH264Frames({{FrameType::VideoFrameKey, frame_size}});
		auto stream = SplitRed(session->packets, 0);

		ASSERT_GT(stream.media.size(), 48u);
		EXPECT_EQ(stream.fec.size(), (stream.media.size() + 6) / 7) << stream.media.size() << " media packets";
		ExpectEveryPacketRecovers(stream);
	}
}

TEST(UlpfecGenerator, FecPacketsTravelWithTheFrameTheyProtect)
{
	auto session = PacketizeH264Frames({{FrameType::VideoFrameKey, 20000}, {FrameType::VideoFrameDelta, 3000}});

	uint16_t last_media_sequence_number = 0;
	uint32_t last_media_timestamp = 0;
	uint64_t last_media_ntp_timestamp = 0;

	for (const auto &packet : session->packets)
	{
		if (packet->PayloadType() != kRedPayloadType)
		{
			continue;
		}

		if (packet->IsUlpfec() == false)
		{
			last_media_sequence_number = packet->SequenceNumber();
			last_media_timestamp = packet->Timestamp();
			last_media_ntp_timestamp = packet->NTPTimestamp();
			continue;
		}

		// After the frame's last packet, and named like it, so a session that picks a track picks its FEC too
		EXPECT_EQ(static_cast<uint16_t>(packet->SequenceNumber() - last_media_sequence_number) < 0x8000, true);
		EXPECT_EQ(packet->Timestamp(), last_media_timestamp);
		EXPECT_EQ(packet->NTPTimestamp(), last_media_ntp_timestamp) << "a sender report pairs the two timestamps of the last packet sent";
		EXPECT_EQ(packet->GetTrackId(), kTrackId);
		EXPECT_EQ(packet->Ssrc(), kSsrc);
		EXPECT_TRUE(packet->IsVideoPacket());
		EXPECT_FALSE(packet->IsKeyframe());
		EXPECT_FALSE(packet->IsFirstPacketOfFrame());
		EXPECT_FALSE(packet->IsLastPacketOfFrame());
		EXPECT_FALSE(packet->Marker());
		EXPECT_NE(packet->Extension(RTP_HEADER_EXTENSION_TRANSPORT_CC_ID), nullptr) << "a session stamps the transport-wide sequence number here";

		const uint8_t *frame_marking = packet->Extension(RTP_HEADER_EXTENSION_FRAMEMARKING_ID);
		ASSERT_NE(frame_marking, nullptr);
		const size_t element_header_size = (packet->GetExtensionType() == RtpHeaderExtension::HeaderType::ONE_BYTE_HEADER) ? 1 : 2;
		EXPECT_EQ(frame_marking[element_header_size], 0) << "an FEC packet marks no frame boundary";
	}
}

TEST(UlpfecGenerator, ShiftSequenceNumberBaseWraps)
{
	// A RED block holding an FEC header, its SN base at the FEC header's third byte
	RtpPacket packet;
	uint8_t *payload = packet.AllocatePayload(RED_HEADER_SIZE + 14);
	std::fill(payload, payload + RED_HEADER_SIZE + 14, 0);
	payload[0] = kUlpfecPayloadType;
	ByteWriter<uint16_t>::WriteBigEndian(&payload[RED_HEADER_SIZE + 2], 0xFFFE);

	EXPECT_TRUE(UlpfecGenerator::ShiftSequenceNumberBase(&packet, 3));
	EXPECT_EQ(ByteReader<uint16_t>::ReadBigEndian(&packet.Payload()[RED_HEADER_SIZE + 2]), 1);

	// Too short to hold an FEC header
	RtpPacket short_packet;
	short_packet.AllocatePayload(4);
	EXPECT_FALSE(UlpfecGenerator::ShiftSequenceNumberBase(&short_packet, 3));
}
