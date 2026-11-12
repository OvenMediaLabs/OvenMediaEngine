//==============================================================================
//
//  OvenMediaEngine - Unit Tests
//
//  Covers: RtpReceiveStatistics (RTCP receiver report block)
//
//==============================================================================
#include <gtest/gtest.h>

#include <algorithm>
#include <initializer_list>

#include "rtcp_info/report_block.h"
#include "rtp_packet.h"
#include "rtp_receive_statistics.h"

namespace
{
constexpr uint32_t kSsrc = 0x11223344;
constexpr uint32_t kVideoRate = 90000;

void Receive(RtpReceiveStatistics &statistics, uint16_t seq)
{
	auto packet = std::make_shared<RtpPacket>();
	packet->SetSsrc(kSsrc);
	packet->SetSequenceNumber(seq);
	packet->SetTimestamp(static_cast<uint32_t>(seq) * 3000);
	statistics.AddReceivedRtpPacket(packet);
}

void Receive(RtpReceiveStatistics &statistics, std::initializer_list<uint16_t> seqs)
{
	for (auto seq : seqs)
	{
		Receive(statistics, seq);
	}
}

// Receives first..last except the listed sequence numbers
void ReceiveRange(RtpReceiveStatistics &statistics, uint16_t first, uint16_t last, std::initializer_list<uint16_t> missing = {})
{
	for (uint16_t seq = first;; seq++)
	{
		bool is_missing = std::find(missing.begin(), missing.end(), seq) != missing.end();
		if (is_missing == false)
		{
			Receive(statistics, seq);
		}

		if (seq == last)
		{
			break;
		}
	}
}

// Cumulative lost is a 24-bit signed field
int32_t CumulativeLost(const std::shared_ptr<ReportBlock> &block)
{
	uint32_t field = block->GetCumulativeLost();
	if (field & 0x800000)
	{
		return static_cast<int32_t>(field | 0xFF000000);
	}
	return static_cast<int32_t>(field);
}
}  // namespace

TEST(RtpReceiveStatistics, InOrderPacketsReportNoLoss)
{
	RtpReceiveStatistics statistics(kSsrc, kVideoRate);
	ReceiveRange(statistics, 100, 109);

	auto block = statistics.GenerateReportBlock();
	EXPECT_EQ(block->GetExtendedHighestSequenceNum(), 109u);
	EXPECT_EQ(CumulativeLost(block), 0);
	EXPECT_EQ(block->GetFractionLost(), 0);
}

TEST(RtpReceiveStatistics, GapCountsAsLost)
{
	RtpReceiveStatistics statistics(kSsrc, kVideoRate);
	ReceiveRange(statistics, 100, 109, {103, 106});

	auto block = statistics.GenerateReportBlock();
	EXPECT_EQ(CumulativeLost(block), 2);
	EXPECT_EQ(block->GetFractionLost(), (2 << 8) / 10);
}

TEST(RtpReceiveStatistics, LatePacketDoesNotMoveHighestBack)
{
	RtpReceiveStatistics statistics(kSsrc, kVideoRate);
	ReceiveRange(statistics, 100, 110, {105});
	Receive(statistics, 105);

	auto block = statistics.GenerateReportBlock();
	EXPECT_EQ(block->GetExtendedHighestSequenceNum(), 110u);
	EXPECT_EQ(CumulativeLost(block), 0);
}

TEST(RtpReceiveStatistics, LatePacketAcrossWrapCountsOneCycle)
{
	RtpReceiveStatistics statistics(kSsrc, kVideoRate);
	ReceiveRange(statistics, 65530, 5, {65533});
	Receive(statistics, 65533);
	Receive(statistics, 6);

	auto block = statistics.GenerateReportBlock();
	EXPECT_EQ(block->GetExtendedHighestSequenceNum(), 65536u + 6);
	EXPECT_EQ(CumulativeLost(block), 0);
}

TEST(RtpReceiveStatistics, IntervalWithLatePacketReportsZeroFraction)
{
	RtpReceiveStatistics statistics(kSsrc, kVideoRate);
	ReceiveRange(statistics, 100, 109, {105});
	auto first = statistics.GenerateReportBlock();
	EXPECT_EQ(CumulativeLost(first), 1);
	EXPECT_GT(first->GetFractionLost(), 0);

	// This interval receives one packet more than it expects
	ReceiveRange(statistics, 110, 118);
	Receive(statistics, 105);
	auto second = statistics.GenerateReportBlock();
	EXPECT_EQ(CumulativeLost(second), 0);
	EXPECT_EQ(second->GetFractionLost(), 0);
}

TEST(RtpReceiveStatistics, IntervalWithoutPacketsReportsZeroFraction)
{
	RtpReceiveStatistics statistics(kSsrc, kVideoRate);
	ReceiveRange(statistics, 100, 109, {105});
	statistics.GenerateReportBlock();

	auto block = statistics.GenerateReportBlock();
	EXPECT_EQ(CumulativeLost(block), 1);
	EXPECT_EQ(block->GetFractionLost(), 0);
}

TEST(RtpReceiveStatistics, DuplicateMakesLostNegative)
{
	RtpReceiveStatistics statistics(kSsrc, kVideoRate);
	ReceiveRange(statistics, 100, 109);
	Receive(statistics, 105);

	auto block = statistics.GenerateReportBlock();
	EXPECT_EQ(block->GetCumulativeLost(), 0xFFFFFFu);
	EXPECT_EQ(CumulativeLost(block), -1);
	EXPECT_EQ(block->GetFractionLost(), 0);
}

TEST(RtpReceiveStatistics, SingleLargeJumpIsIgnored)
{
	RtpReceiveStatistics statistics(kSsrc, kVideoRate);
	ReceiveRange(statistics, 100, 109);
	Receive(statistics, {30000, 110});

	auto block = statistics.GenerateReportBlock();
	EXPECT_EQ(block->GetExtendedHighestSequenceNum(), 110u);
	EXPECT_EQ(CumulativeLost(block), 0);
}

TEST(RtpReceiveStatistics, ConfirmedLargeJumpRestartsCounting)
{
	RtpReceiveStatistics statistics(kSsrc, kVideoRate);
	ReceiveRange(statistics, 100, 109, {105});
	Receive(statistics, {30000, 30001, 30002});

	auto block = statistics.GenerateReportBlock();
	EXPECT_EQ(block->GetExtendedHighestSequenceNum(), 30002u);
	EXPECT_EQ(CumulativeLost(block), 0);
}
