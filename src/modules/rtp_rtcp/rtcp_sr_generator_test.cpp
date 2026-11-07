//==============================================================================
//
//  OvenMediaEngine - Unit Tests
//
//  Covers: RtcpSRGenerator
//
//==============================================================================
#include <gtest/gtest.h>

#include <vector>

#include "rtcp_info/rtcp_sr_generator.h"
#include "rtcp_info/sender_report.h"
#include "rtp_packet.h"

namespace
{
constexpr uint32_t kSsrc = 0x11223344;
constexpr uint32_t kVideoRate = 90000;

void SendPackets(RtcpSRGenerator &generator, int count, size_t payload_size)
{
	std::vector<uint8_t> payload(payload_size, 0xAB);

	for (int i = 0; i < count; i++)
	{
		auto packet = std::make_shared<RtpPacket>();
		packet->SetSsrc(kSsrc);
		packet->SetPayload(payload.data(), payload.size());
		generator.AddRTPPacketInfo(packet);
	}
}

std::shared_ptr<SenderReport> PopReport(RtcpSRGenerator &generator)
{
	auto rtcp_packet = generator.PopRtcpSRPacket();
	if (rtcp_packet == nullptr)
	{
		return nullptr;
	}

	auto report = std::make_shared<SenderReport>();
	report->Parse(*rtcp_packet);
	return report;
}
}  // namespace

TEST(RtcpSRGenerator, CountsAreRunningTotals)
{
	RtcpSRGenerator generator(kSsrc, kVideoRate);

	SendPackets(generator, 10, 100);
	auto first = PopReport(generator);
	ASSERT_NE(first, nullptr);
	EXPECT_EQ(first->GetPacketCount(), 10u);
	EXPECT_EQ(first->GetOctetCount(), 1000u);

	SendPackets(generator, 5, 200);
	auto second = PopReport(generator);
	ASSERT_NE(second, nullptr);
	EXPECT_EQ(second->GetPacketCount(), 15u);
	EXPECT_EQ(second->GetOctetCount(), 2000u);
}

TEST(RtcpSRGenerator, NoReportWithoutNewPackets)
{
	RtcpSRGenerator generator(kSsrc, kVideoRate);
	EXPECT_EQ(generator.PopRtcpSRPacket(), nullptr);

	SendPackets(generator, 3, 100);
	ASSERT_NE(generator.PopRtcpSRPacket(), nullptr);
	EXPECT_EQ(generator.PopRtcpSRPacket(), nullptr);

	SendPackets(generator, 1, 100);
	auto report = PopReport(generator);
	ASSERT_NE(report, nullptr);
	EXPECT_EQ(report->GetPacketCount(), 4u);
	EXPECT_EQ(report->GetOctetCount(), 400u);
}
