//==============================================================================
//
//  OvenMediaEngine - Unit Tests
//
//  Covers: RtpPacketizerAV1 (AV1 RTP Specification)
//          - aggregation header Z/Y/W/N
//          - LEB128 length-prefixed elements + last-element length omission
//          - Temporal Delimiter removal
//          - MTU fragmentation across packets
//          - round-trip against RtpDepacketizerAV1
//
//==============================================================================
#include <gtest/gtest.h>

#include "rtp_depacketizer_av1.h"
#include "rtp_packetizer_av1.h"
#include "rtp_packet.h"

namespace
{
	// A generous MTU that keeps any small temporal unit in a single packet.
	constexpr size_t kLargeMtu = 1200;

	std::vector<std::vector<uint8_t>> Packetize(const std::vector<uint8_t> &temporal_unit, size_t max_payload_len)
	{
		RtpPacketizerAV1 packetizer;
		const size_t	 num_packets = packetizer.SetPayloadData(max_payload_len, 0, nullptr, FrameType::VideoFrameDelta,
																 temporal_unit.data(), temporal_unit.size(), nullptr);

		std::vector<std::vector<uint8_t>> payloads;
		for (size_t i = 0; i < num_packets; i++)
		{
			RtpPacket packet;
			EXPECT_TRUE(packetizer.NextPacket(&packet));
			const uint8_t *p = packet.Payload();
			payloads.emplace_back(p, p + packet.PayloadSize());
		}

		// The packetizer is drained.
		RtpPacket extra;
		EXPECT_FALSE(packetizer.NextPacket(&extra));

		return payloads;
	}

	std::vector<uint8_t> RoundTrip(const std::vector<uint8_t> &temporal_unit, size_t max_payload_len)
	{
		auto payloads = Packetize(temporal_unit, max_payload_len);

		std::vector<std::shared_ptr<ov::Data>> rtp_payloads;
		for (const auto &payload : payloads)
		{
			rtp_payloads.push_back(std::make_shared<ov::Data>(payload.data(), payload.size()));
		}

		RtpDepacketizerAV1 depacketizer;
		auto			   out = depacketizer.ParseAndAssembleFrame(rtp_payloads);
		if (out == nullptr)
		{
			return {};
		}

		const auto *p = out->GetDataAs<uint8_t>();
		return std::vector<uint8_t>(p, p + out->GetLength());
	}

	// Low-overhead OBUs (obu_has_size_field == 1) used as packetizer input:
	//   SequenceHeader (type 1) header = 0x0A, Frame (type 6) header = 0x32, TemporalDelimiter (type 2) header = 0x12.
}  // namespace

// One small Frame temporal unit fits a single packet; round-trip reproduces the input verbatim.
TEST(RtpPacketizerAV1, RoundTripSingleFrame)
{
	const std::vector<uint8_t> tu = {0x32, 0x02, 0xAA, 0xBB};	// Frame OBU, size 2

	auto payloads = Packetize(tu, kLargeMtu);
	ASSERT_EQ(payloads.size(), 1u);
	// agg=0x10 (Z=0,Y=0,W=1,N=0), element {0x30,0xAA,0xBB} (size field stripped).
	EXPECT_EQ(payloads[0], (std::vector<uint8_t>{0x10, 0x30, 0xAA, 0xBB}));

	EXPECT_EQ(RoundTrip(tu, kLargeMtu), tu);
}

// A sequence-header + frame temporal unit packs into one packet, sets the N bit, and length-prefixes
// every element but the last.
TEST(RtpPacketizerAV1, RoundTripSequenceHeaderAndFrameSetsNBit)
{
	const std::vector<uint8_t> tu = {0x0A, 0x01, 0xAA, 0x32, 0x02, 0xBB, 0xCC};

	auto payloads = Packetize(tu, kLargeMtu);
	ASSERT_EQ(payloads.size(), 1u);
	// agg: W=2 (0x20) | N=1 (0x08) = 0x28; then leb(2)+{0x08,0xAA}; last element {0x30,0xBB,0xCC}.
	EXPECT_EQ(payloads[0], (std::vector<uint8_t>{0x28, 0x02, 0x08, 0xAA, 0x30, 0xBB, 0xCC}));

	EXPECT_EQ(RoundTrip(tu, kLargeMtu), tu);
}

// The Temporal Delimiter OBU is removed; the round-trip yields the temporal unit without it.
TEST(RtpPacketizerAV1, TemporalDelimiterDropped)
{
	const std::vector<uint8_t> tu_with_td = {0x12, 0x00, 0x32, 0x02, 0xAA, 0xBB};	// TD + Frame
	const std::vector<uint8_t> frame_only = {0x32, 0x02, 0xAA, 0xBB};

	auto payloads = Packetize(tu_with_td, kLargeMtu);
	ASSERT_EQ(payloads.size(), 1u);
	EXPECT_EQ(payloads[0], (std::vector<uint8_t>{0x10, 0x30, 0xAA, 0xBB}));

	EXPECT_EQ(RoundTrip(tu_with_td, kLargeMtu), frame_only);
}

// A frame larger than the MTU is fragmented across packets with correct Z/Y bits; reassembly restores it.
TEST(RtpPacketizerAV1, FragmentedAcrossPackets)
{
	// Frame OBU with a 10-byte payload (element bytes = 1 header + 10 payload = 11).
	std::vector<uint8_t> tu = {0x32, 0x0A};
	for (uint8_t i = 0; i < 10; i++)
	{
		tu.push_back(static_cast<uint8_t>(0xA0 + i));
	}

	// budget = max_payload_len - 1 (agg header) = 4 bytes of element data per packet -> 11 bytes => 3 packets.
	auto payloads = Packetize(tu, 5);
	ASSERT_EQ(payloads.size(), 3u);

	// Every packet carries a single OBU element (W=1).
	EXPECT_EQ(payloads[0][0], 0x10 | 0x40);			   // W=1, Y=1
	EXPECT_EQ(payloads[1][0], 0x10 | 0x80 | 0x40);	   // W=1, Z=1, Y=1
	EXPECT_EQ(payloads[2][0], 0x10 | 0x80);			   // W=1, Z=1, Y=0

	EXPECT_EQ(RoundTrip(tu, 5), tu);
}

// Two OBUs that do not fit one packet split across packets and still reassemble exactly.
TEST(RtpPacketizerAV1, TwoObusSmallMtuRoundTrip)
{
	std::vector<uint8_t> tu = {0x0A, 0x03, 0x11, 0x22, 0x33};	// SequenceHeader, payload 3 bytes
	tu.insert(tu.end(), {0x32, 0x06, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46});	// Frame, payload 6 bytes

	EXPECT_EQ(RoundTrip(tu, 7), tu);
}

// Empty input and a temporal unit consisting solely of a Temporal Delimiter produce no packets.
TEST(RtpPacketizerAV1, EmptyAndTemporalDelimiterOnlyProduceNoPackets)
{
	EXPECT_TRUE(Packetize({}, kLargeMtu).empty());
	EXPECT_TRUE(Packetize({0x12, 0x00}, kLargeMtu).empty());	// TD only
}
