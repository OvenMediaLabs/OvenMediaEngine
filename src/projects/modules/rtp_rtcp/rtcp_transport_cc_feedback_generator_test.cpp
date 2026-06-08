//==============================================================================
//
//  OvenMediaEngine - Unit Tests
//
//  Covers: RtcpTransportCcFeedbackGenerator (atomic check-and-generate)
//
//==============================================================================
#include <gtest/gtest.h>

#include <atomic>
#include <thread>
#include <vector>

#include "rtcp_info/rtcp_transport_cc_feedback_generator.h"
#include "rtp_header_extension/rtp_header_extension_transport_cc.h"
#include "rtp_header_extension/rtp_header_extensions.h"
#include "rtp_packet.h"

namespace
{
constexpr uint8_t kExtId = 5;
constexpr uint32_t kSenderSsrc = 0x11111111;
constexpr uint32_t kMediaSsrc = 0xDEADBEEF;

// Build a received RTP packet carrying a transport-wide sequence number.
// SetExtensions writes only the wire buffer (GetExtension reads the parsed
// map), so serialize and re-parse to populate it the way the receive path does.
std::shared_ptr<RtpPacket> MakeTwccPacket(uint16_t tw_seq, uint16_t rtp_seq)
{
	auto src = std::make_shared<RtpPacket>();
	src->SetPayloadType(96);
	src->SetSequenceNumber(rtp_seq);
	src->SetSsrc(kMediaSsrc);
	src->SetTimestamp(static_cast<uint32_t>(rtp_seq) * 90);

	auto ext = std::make_shared<RtpHeaderExtensionTransportCc>(kExtId);
	ext->SetSequenceNumber(tw_seq);

	RtpHeaderExtensions extensions;
	extensions.AddExtention(ext);
	src->SetExtensions(extensions);

	uint8_t payload[16] = {0};
	src->SetPayload(payload, sizeof(payload));

	return std::make_shared<RtpPacket>(src->GetData());
}
}  // namespace

// Sanity: the helper produces a packet whose transport-wide seq round-trips.
// If this fails, the generator tests below are testing the wrong thing.
TEST(RtcpTransportCcFeedbackGenerator, HelperRoundTripsExtension)
{
	auto packet = MakeTwccPacket(1234, 1000);
	auto value = packet->GetExtension<uint16_t>(kExtId);
	ASSERT_TRUE(value.has_value());
	EXPECT_EQ(*value, 1234);
}

// No packets accumulated -> nothing to send regardless of elapsed time.
TEST(RtcpTransportCcFeedbackGenerator, EmptyReturnsNull)
{
	RtcpTransportCcFeedbackGenerator gen(kExtId, kSenderSsrc);
	EXPECT_EQ(gen.GenerateTransportCcMessageIfElapsed(0), nullptr);
}

// The fix: check-and-consume happen under one lock. After a message is
// generated the batch is consumed, so an immediate second call returns null
// even though the 0ms interval check would otherwise pass again.
TEST(RtcpTransportCcFeedbackGenerator, GeneratesThenResets)
{
	RtcpTransportCcFeedbackGenerator gen(kExtId, kSenderSsrc);
	ASSERT_TRUE(gen.AddReceivedRtpPacket(MakeTwccPacket(1, 1000)));
	ASSERT_TRUE(gen.AddReceivedRtpPacket(MakeTwccPacket(2, 1001)));

	ASSERT_NE(gen.GenerateTransportCcMessageIfElapsed(0), nullptr);
	EXPECT_EQ(gen.GenerateTransportCcMessageIfElapsed(0), nullptr);
}

// When the interval has not elapsed the batch must be kept, not consumed.
TEST(RtcpTransportCcFeedbackGenerator, NotElapsedKeepsBatch)
{
	RtcpTransportCcFeedbackGenerator gen(kExtId, kSenderSsrc);
	ASSERT_TRUE(gen.AddReceivedRtpPacket(MakeTwccPacket(1, 1000)));

	EXPECT_EQ(gen.GenerateTransportCcMessageIfElapsed(100000), nullptr);
	EXPECT_NE(gen.GenerateTransportCcMessageIfElapsed(0), nullptr);
}

// Many threads add and generate against one generator: must not crash or
// deadlock, and must still produce feedback. Run under ThreadSanitizer to
// catch data races on the generator's internal state.
TEST(RtcpTransportCcFeedbackGenerator, ConcurrentAddAndGenerate)
{
	RtcpTransportCcFeedbackGenerator gen(kExtId, kSenderSsrc);

	std::vector<std::shared_ptr<RtpPacket>> pool;
	for (int i = 0; i < 256; i++)
	{
		pool.push_back(MakeTwccPacket(static_cast<uint16_t>(i), static_cast<uint16_t>(1000 + i)));
	}

	constexpr int kThreads = 8;
	constexpr int kIters = 500;
	std::atomic<int> generated{0};

	std::vector<std::thread> threads;
	for (int t = 0; t < kThreads; t++)
	{
		threads.emplace_back([&, t]() {
			for (int i = 0; i < kIters; i++)
			{
				gen.AddReceivedRtpPacket(pool[(t * kIters + i) % pool.size()]);
				if (gen.GenerateTransportCcMessageIfElapsed(0) != nullptr)
				{
					generated.fetch_add(1);
				}
			}
		});
	}
	for (auto &thread : threads)
	{
		thread.join();
	}

	EXPECT_GT(generated.load(), 0);
}
