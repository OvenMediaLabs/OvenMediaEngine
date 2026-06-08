//==============================================================================
//
//  OvenMediaEngine - Unit Tests
//
//  Covers: RtpRtcp receive path under concurrent same-track delivery
//
//==============================================================================
#include <gtest/gtest.h>

#include <atomic>
#include <iostream>
#include <thread>
#include <vector>

#include "base/info/media_track.h"
#include "base/ovlibrary/log.h"
#include "rtp_header_extension/rtp_header_extension_transport_cc.h"
#include "rtp_header_extension/rtp_header_extensions.h"
#include "rtp_packet.h"
#include "rtp_rtcp.h"

namespace
{
constexpr uint32_t kTrackId = 1;
constexpr uint32_t kSsrc = 0x12345678;
constexpr uint8_t kTwccExtId = 5;

class CountingObserver : public RtpRtcpInterface
{
public:
	void OnRtpFrameReceived(const std::vector<std::shared_ptr<RtpPacket>> &) override
	{
		_frames.fetch_add(1, std::memory_order_relaxed);
	}
	void OnRtcpReceived(const std::shared_ptr<RtcpInfo> &) override
	{
		_rtcp.fetch_add(1, std::memory_order_relaxed);
	}

	std::atomic<int> _frames{0};
	std::atomic<int> _rtcp{0};
};

std::shared_ptr<MediaTrack> MakeH264Track()
{
	auto track = std::make_shared<MediaTrack>();
	track->SetId(kTrackId);
	track->SetMediaType(cmn::MediaType::Video);
	track->SetCodecId(cmn::MediaCodecId::H264);
	track->SetTimeBase(1, 90000);
	track->SetOriginBitstream(cmn::BitstreamFormat::H264_RTP_RFC_6184);
	return track;
}

// Single-NAL H264 packet (first byte type 5 = IDR) carrying a transport-wide
// sequence number, marker set so the boundary detector treats it as a complete
// one-packet frame. Serialized to wire form the receive path re-parses.
std::shared_ptr<ov::Data> MakeH264RtpData(uint16_t seq, uint32_t timestamp, uint16_t tw_seq)
{
	auto packet = std::make_shared<RtpPacket>();
	packet->SetPayloadType(96);
	packet->SetMarker(true);
	packet->SetSequenceNumber(seq);
	packet->SetSsrc(kSsrc);
	packet->SetTimestamp(timestamp);

	auto ext = std::make_shared<RtpHeaderExtensionTransportCc>(kTwccExtId);
	ext->SetSequenceNumber(tw_seq);
	RtpHeaderExtensions extensions;
	extensions.AddExtention(ext);
	packet->SetExtensions(extensions);

	const uint8_t payload[] = {0x65, 0x88, 0x84, 0x21, 0x00, 0x10, 0x20, 0x30};
	packet->SetPayload(payload, sizeof(payload));

	return packet->GetData();
}
}  // namespace

// The per-packet info/warning logs (NACK / transport-cc reacting to the
// concurrent, out-of-order synthetic load) are expected noise here, not bugs.
// Silence info/warning for these tags so a --gtest_repeat=1000 run stays
// readable, but keep error/critical visible: a real failure (e.g. a corrupted
// map surfacing as a logte) must still print. Crashes, hangs, gtest asserts,
// and TSan race reports are independent of OME logging and unaffected.
class RtpRtcpConcurrentReceive : public ::testing::Test
{
protected:
	void SetUp() override
	{
		for (const char *tag : {"RtpRtcp", "RtpNack", "RTCP", "transport-cc"})
		{
			::ov_log_set_enable(tag, OVLogLevelError, true);
		}
	}
	void TearDown() override
	{
		::ov_log_reset_enable();
	}
};

// Simulates the ICE candidate-pair switch this PR guards against: the same
// session's media briefly arrives on two socket-pool worker threads at once.
// Many threads push the SAME track's RTP packets through the full receive path
// (track lookup, receive stats, NACK, transport-cc, jitter buffer) concurrently.
//
// Build with -DOME_SANITIZE_THREAD=ON and run with --gtest_repeat=1000
// --gtest_shuffle to surface data races on the shared receive state; without
// TSan this still catches crashes / deadlocks.
TEST_F(RtpRtcpConcurrentReceive, SameTrackFromManyThreads)
{
	auto observer = std::make_shared<CountingObserver>();
	auto rtp_rtcp = std::make_shared<RtpRtcp>(observer);

	RtpRtcp::RtpTrackIdentifier rtp_track_id(kTrackId);
	rtp_track_id.ssrc = kSsrc;

	ASSERT_TRUE(rtp_rtcp->AddRtpReceiver(MakeH264Track(), rtp_track_id));
	rtp_rtcp->EnableNack(kTrackId, kSsrc, 400);
	rtp_rtcp->EnableTransportCcFeedback(kTwccExtId);
	ASSERT_TRUE(rtp_rtcp->Start());

	constexpr int kThreads = 8;
	constexpr int kIters = 500;

	// One near-sequential stream split across threads via a shared counter,
	// like the same RTP flow being briefly drained by two socket workers at
	// once. Thread scheduling still reorders arrivals, contending every lock.
	std::atomic<uint32_t> next_seq{0};
	std::atomic<int> in_flight{0};    // threads currently inside the receive path
	std::atomic<int> peak_in_flight{0};
	std::vector<std::thread> threads;
	for (int t = 0; t < kThreads; t++)
	{
		threads.emplace_back([&]() {
			for (int i = 0; i < kIters; i++)
			{
				auto n = next_seq.fetch_add(1, std::memory_order_relaxed);
				auto seq = static_cast<uint16_t>(n);
				auto data = MakeH264RtpData(seq, n * 3000, seq);

				int cur = in_flight.fetch_add(1, std::memory_order_relaxed) + 1;
				for (int prev = peak_in_flight.load(std::memory_order_relaxed);
					 cur > prev && !peak_in_flight.compare_exchange_weak(prev, cur);)
				{
				}

				rtp_rtcp->OnDataReceivedFromNextNode(NodeType::Srtp, data);

				in_flight.fetch_sub(1, std::memory_order_relaxed);
			}
		});
	}
	for (auto &thread : threads)
	{
		thread.join();
	}

	rtp_rtcp->Stop();

	// Visible evidence the run actually overlapped threads in the receive path
	// (peak > 1) and ran the path end to end (frames reached the observer, so
	// packets weren't dropped early). The real race verdict comes from TSan.
	std::cout << "[ INFO     ] fed " << (kThreads * kIters) << " packets / " << kThreads
			  << " threads, peak " << peak_in_flight.load() << " concurrent in receive, "
			  << observer->_frames.load() << " frames delivered\n";

	EXPECT_GE(peak_in_flight.load(), 2);
	EXPECT_GT(observer->_frames.load(), 0);
}
