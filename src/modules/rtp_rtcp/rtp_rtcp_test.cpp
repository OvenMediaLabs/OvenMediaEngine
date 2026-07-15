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
#include "rtx_rtp_packet.h"

namespace
{
constexpr uint32_t kVideoTrack = 1;
constexpr uint32_t kAudioTrack = 2;
constexpr uint32_t kMediaSsrc = 0x11111111;
constexpr uint32_t kRtxSsrc = 0x22222222;
constexpr uint32_t kAudioSsrc = 0x33333333;
constexpr uint8_t kMediaPt = 96;
constexpr uint8_t kRtxPt = 97;
constexpr uint8_t kAudioPt = 111;
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

std::shared_ptr<MediaTrack> MakeTrack(uint32_t id, cmn::MediaType type, cmn::MediaCodecId codec,
									  int32_t clock_rate, cmn::BitstreamFormat bitstream)
{
	auto track = std::make_shared<MediaTrack>();
	track->SetId(id);
	track->SetMediaType(type);
	track->SetCodecId(codec);
	track->SetTimeBase(1, clock_rate);
	track->SetOriginBitstream(bitstream);
	return track;
}

void AddTwcc(RtpPacket &packet, uint16_t tw_seq)
{
	auto ext = std::make_shared<RtpHeaderExtensionTransportCc>(kTwccExtId);
	ext->SetSequenceNumber(tw_seq);
	RtpHeaderExtensions extensions;
	extensions.AddExtention(ext);
	packet.SetExtensions(extensions);
}

// H264 single-NAL (IDR) media packet. with_twcc adds the transport-wide seq
// extension; empty_payload yields a padding-only (0-length payload) packet.
std::shared_ptr<ov::Data> MakeMediaData(uint16_t seq, uint32_t ts, bool with_twcc, bool empty_payload)
{
	RtpPacket packet;
	packet.SetPayloadType(kMediaPt);
	packet.SetMarker(true);
	packet.SetSequenceNumber(seq);
	packet.SetSsrc(kMediaSsrc);
	packet.SetTimestamp(ts);
	if (with_twcc)
	{
		AddTwcc(packet, seq);
	}
	if (empty_payload == false)
	{
		const uint8_t nal[] = {0x65, 0x88, 0x84, 0x21, 0x00, 0x10, 0x20, 0x30};
		packet.SetPayload(nal, sizeof(nal));
	}
	return packet.GetData();
}

// RTX-wrapped (RFC 4588) retransmission of an original media packet.
std::shared_ptr<ov::Data> MakeRtxData(uint16_t rtx_seq, uint16_t original_seq, uint32_t ts)
{
	RtpPacket original;
	original.SetPayloadType(kMediaPt);
	original.SetMarker(true);
	original.SetSequenceNumber(original_seq);
	original.SetSsrc(kMediaSsrc);
	original.SetTimestamp(ts);
	AddTwcc(original, original_seq);
	const uint8_t nal[] = {0x65, 0x88, 0x84, 0x21, 0x00, 0x10};
	original.SetPayload(nal, sizeof(nal));

	RtxRtpPacket rtx(kRtxSsrc, kRtxPt, original);
	rtx.SetSequenceNumber(rtx_seq);
	return rtx.GetData();
}

// RTX padding-only probe: payload smaller than the OSN header, so Unpack
// returns nullptr (the path that reports the wire seq to transport-cc).
std::shared_ptr<ov::Data> MakeRtxProbeData(uint16_t rtx_seq, uint16_t tw_seq)
{
	RtpPacket packet;
	packet.SetPayloadType(kRtxPt);
	packet.SetSequenceNumber(rtx_seq);
	packet.SetSsrc(kRtxSsrc);
	packet.SetTimestamp(static_cast<uint32_t>(rtx_seq) * 3000);
	AddTwcc(packet, tw_seq);
	return packet.GetData();
}

std::shared_ptr<ov::Data> MakeOpusData(uint16_t seq, uint32_t ts)
{
	RtpPacket packet;
	packet.SetPayloadType(kAudioPt);
	packet.SetMarker(true);
	packet.SetSequenceNumber(seq);
	packet.SetSsrc(kAudioSsrc);
	packet.SetTimestamp(ts);
	const uint8_t opus[] = {0xfc, 0xff, 0xfe, 0x01, 0x02};
	packet.SetPayload(opus, sizeof(opus));
	return packet.GetData();
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
// Many threads push a MIX of packet types through the full receive path
// concurrently so every guarded path runs contended:
//   - H264 media with / without the transport-cc extension
//   - RTX retransmissions (unwrap) and RTX padding-only probes
//   - padding-only packets and deliberate seq gaps (NACK firing/retry)
//   - Opus media (minimal jitter buffer)
//
// Build with -DOME_SANITIZE_THREAD=ON and run with --gtest_repeat=1000
// --gtest_shuffle to surface data races on the shared receive state; without
// TSan this still catches crashes / deadlocks.
TEST_F(RtpRtcpConcurrentReceive, MixedTrafficFromManyThreads)
{
	auto observer = std::make_shared<CountingObserver>();
	auto rtp_rtcp = std::make_shared<RtpRtcp>(observer);

	RtpRtcp::RtpTrackIdentifier video_id(kVideoTrack);
	video_id.ssrc = kMediaSsrc;
	RtpRtcp::RtpTrackIdentifier audio_id(kAudioTrack);
	audio_id.ssrc = kAudioSsrc;

	ASSERT_TRUE(rtp_rtcp->AddRtpReceiver(
		MakeTrack(kVideoTrack, cmn::MediaType::Video, cmn::MediaCodecId::H264, 90000,
				  cmn::BitstreamFormat::H264_RTP_RFC_6184),
		video_id));
	ASSERT_TRUE(rtp_rtcp->AddRtpReceiver(
		MakeTrack(kAudioTrack, cmn::MediaType::Audio, cmn::MediaCodecId::Opus, 48000,
				  cmn::BitstreamFormat::OPUS_RTP_RFC_7587),
		audio_id));

	rtp_rtcp->EnableNack(kVideoTrack, kMediaSsrc, 400);
	rtp_rtcp->RegisterRtxStream(kRtxSsrc, kMediaSsrc, kRtxPt, kMediaPt);
	rtp_rtcp->EnableTransportCcFeedback(kTwccExtId);
	ASSERT_TRUE(rtp_rtcp->Start());

	constexpr int kThreads = 8;
	constexpr int kIters = 600;

	std::atomic<uint32_t> op{0};          // global op counter -> scenario select
	std::atomic<uint32_t> media_seq{0};   // video stream sequence
	std::atomic<uint32_t> rtx_seq{0};     // rtx stream sequence
	std::atomic<uint32_t> audio_seq{0};   // opus stream sequence
	std::atomic<int> in_flight{0};
	std::atomic<int> peak_in_flight{0};

	auto feed = [&](const std::shared_ptr<ov::Data> &data) {
		int cur = in_flight.fetch_add(1, std::memory_order_relaxed) + 1;
		for (int prev = peak_in_flight.load(std::memory_order_relaxed);
			 cur > prev && !peak_in_flight.compare_exchange_weak(prev, cur);)
		{
		}
		rtp_rtcp->OnDataReceivedFromNextNode(NodeType::Srtp, data);
		in_flight.fetch_sub(1, std::memory_order_relaxed);
	};

	std::vector<std::thread> threads;
	for (int t = 0; t < kThreads; t++)
	{
		threads.emplace_back([&]() {
			for (int i = 0; i < kIters; i++)
			{
				auto n = op.fetch_add(1, std::memory_order_relaxed);
				switch (n % 8)
				{
					case 0:
					case 1:
					case 2:
					{
						// Normal media; every 11th seq is consumed but not sent
						// to leave a gap the NACK generator must chase.
						auto s = media_seq.fetch_add(1, std::memory_order_relaxed);
						if (n % 11 != 0)
						{
							feed(MakeMediaData(static_cast<uint16_t>(s), s * 3000, true, false));
						}
						break;
					}
					case 3:
					{
						// Media without the transport-cc extension.
						auto s = media_seq.fetch_add(1, std::memory_order_relaxed);
						feed(MakeMediaData(static_cast<uint16_t>(s), s * 3000, false, false));
						break;
					}
					case 4:
					{
						// RTX retransmission of a recent media seq.
						auto r = rtx_seq.fetch_add(1, std::memory_order_relaxed);
						auto osn = static_cast<uint16_t>(media_seq.load(std::memory_order_relaxed));
						feed(MakeRtxData(static_cast<uint16_t>(r), osn, osn * 3000));
						break;
					}
					case 5:
					{
						// RTX padding-only probe.
						auto r = rtx_seq.fetch_add(1, std::memory_order_relaxed);
						feed(MakeRtxProbeData(static_cast<uint16_t>(r), static_cast<uint16_t>(r)));
						break;
					}
					case 6:
					{
						// Padding-only media (0-length payload).
						auto s = media_seq.fetch_add(1, std::memory_order_relaxed);
						feed(MakeMediaData(static_cast<uint16_t>(s), s * 3000, true, true));
						break;
					}
					default:
					{
						// Opus -> minimal jitter buffer.
						auto s = audio_seq.fetch_add(1, std::memory_order_relaxed);
						feed(MakeOpusData(static_cast<uint16_t>(s), s * 960));
						break;
					}
				}
			}
		});
	}
	for (auto &thread : threads)
	{
		thread.join();
	}

	rtp_rtcp->Stop();

	// peak_in_flight is printed (not asserted): observed thread overlap depends
	// on the scheduler, so on a low-core / loaded host it can read 1 even when
	// the code is correct. The race verdict comes from TSan; the only hard
	// check is that the path actually ran end to end (no total packet drop).
	std::cout << "[ INFO     ] " << op.load() << " ops / " << kThreads
			  << " threads, peak " << peak_in_flight.load() << " concurrent in receive, "
			  << observer->_frames.load() << " frames delivered\n";

	EXPECT_GT(observer->_frames.load(), 0);
}
