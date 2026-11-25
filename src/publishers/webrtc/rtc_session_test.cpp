//==============================================================================
//
//  OvenMediaEngine
//
//  Copyright (c) 2026 OvenMediaLabs. All rights reserved.
//
//==============================================================================
#include "rtc_session.h"

#include <base/info/application.h>
#include <base/ovlibrary/byte_io.h>
#include <gtest/gtest.h>
#include <modules/rtp_rtcp/rtcp_info/nack.h>

#include "rtc_application.h"
#include "rtc_common_types.h"
#include "rtc_stream.h"

namespace
{
	class TestApplicationInfo : public info::Application
	{
	public:
		using info::Application::GetInvalidApplication;
	};

	// Stands in for the SRTP node and keeps every RTP packet the session sends.
	class RecordingNode : public ov::Node
	{
	public:
		RecordingNode()
			: ov::Node(NodeType::Srtp)
		{
		}

		bool OnDataReceivedFromPrevNode(NodeType from_node, const std::shared_ptr<ov::Data> &data) override
		{
			if (from_node == NodeType::Rtp)
			{
				rtp_packets.push_back(data);
			}
			return true;
		}

		bool OnDataReceivedFromNextNode(NodeType, const std::shared_ptr<const ov::Data> &) override
		{
			return true;
		}

		std::vector<std::shared_ptr<ov::Data>> rtp_packets;
	};
}  // namespace

// Friended by RtcSession and RtcStream; TEST_F bodies go through its helpers.
class RtcSessionNackTest : public ::testing::Test
{
protected:
	static constexpr uint32_t kVideoTrackId = 1;
	static constexpr uint32_t kVideoSsrc = 1000;
	static constexpr uint8_t kVideoPayloadType = static_cast<uint8_t>(FixedRtcPayloadType::VP8_PAYLOAD_TYPE);

	void SetUp() override
	{
		auto app = std::make_shared<RtcApplication>(nullptr, TestApplicationInfo::GetInvalidApplication(), nullptr, nullptr);
		auto stream = RtcStream::Create(app, info::Stream(*app, StreamSourceType::Ovt), 0);
		ASSERT_TRUE(stream->EnterStart());

		auto video = std::make_shared<MediaTrack>();
		video->SetId(kVideoTrackId);
		video->SetCodecId(cmn::MediaCodecId::Vp8);
		// RTX is off in the default config, so the stream has no history yet.
		stream->AddRtpHistory(video);
		history = stream->GetHistory(kVideoTrackId, kVideoPayloadType);
		ASSERT_NE(history, nullptr);

		session = std::make_shared<RtcSession>(info::Session(*stream, 1), nullptr, nullptr, stream, "test",
											   std::make_shared<SessionDescription>(SessionDescription::SdpType::Offer), nullptr,
											   nullptr, 0, nullptr);
		session->_rtx_enabled = true;
		session->_video_ssrc = kVideoSsrc;
		session->_rtp_rtcp = std::make_shared<RtpRtcp>(nullptr);
		ASSERT_TRUE(session->_rtp_rtcp->AddRtpSender(kVideoPayloadType, kVideoSsrc, 90000, "test"));
		session->_rtp_rtcp->RegisterNextNode(wire);
		ASSERT_TRUE(session->_rtp_rtcp->Start());
	}

	void LogSentVideoPacket(uint16_t sequence_number)
	{
		auto packet = MakePacket(sequence_number, true);
		history->StoreRtpPacket(packet);
		ASSERT_TRUE(session->RecordRtpSent(packet, sequence_number, 0));
	}

	static std::shared_ptr<RtpPacket> MakePacket(uint16_t sequence_number, bool video)
	{
		auto packet = std::make_shared<RtpPacket>();
		packet->SetTrackId(kVideoTrackId);
		packet->SetVideoPacket(video);
		packet->SetPayloadType(kVideoPayloadType);
		packet->SetSequenceNumber(sequence_number);
		return packet;
	}

	bool RecordPacket(bool rtx, const std::shared_ptr<RtpPacket> &packet)
	{
		session->_rtx_enabled = rtx;
		return session->RecordRtpSent(packet, 10, 0);
	}

	bool HasRecord(uint16_t sequence_number)
	{
		return session->TraceRtpSentByVideoSeqNo(sequence_number) != nullptr;
	}

	void ReceiveNack(const std::vector<uint16_t> &lost_ids)
	{
		auto nack = std::make_shared<NACK>();
		nack->SetMediaSsrc(kVideoSsrc);
		for (auto id : lost_ids)
		{
			nack->AddLostId(id);
		}
		session->ProcessNACK(nack);
	}

	uint16_t NextRtxSequenceNumber() const
	{
		return session->_rtx_sequence_number;
	}

	std::shared_ptr<RtpHistory> history;
	std::shared_ptr<RtcSession> session;
	std::shared_ptr<RecordingNode> wire = std::make_shared<RecordingNode>();
};

TEST_F(RtcSessionNackTest, RetransmitsEveryLostPacketInOneNack)
{
	LogSentVideoPacket(10);
	LogSentVideoPacket(11);
	LogSentVideoPacket(12);
	auto first_rtx_sequence_number = NextRtxSequenceNumber();

	ReceiveNack({10, 11, 12});

	ASSERT_EQ(wire->rtp_packets.size(), 3u);
	for (uint16_t i = 0; i < 3; i++)
	{
		RtpPacket rtx(wire->rtp_packets[i]);
		EXPECT_EQ(rtx.SequenceNumber(), static_cast<uint16_t>(first_rtx_sequence_number + i));
		// RFC 4588: the RTX payload starts with the original sequence number.
		EXPECT_EQ(ByteReader<uint16_t>::ReadBigEndian(rtx.Payload()), 10 + i);
	}
}

TEST_F(RtcSessionNackTest, DoesNotRecordVideoWhenRtxIsDisabled)
{
	EXPECT_FALSE(RecordPacket(false, MakePacket(10, true)));
	EXPECT_FALSE(HasRecord(10));
}

TEST_F(RtcSessionNackTest, DoesNotRecordAudioWhenRtxIsEnabled)
{
	EXPECT_FALSE(RecordPacket(true, MakePacket(10, false)));
	EXPECT_FALSE(HasRecord(10));
}

TEST_F(RtcSessionNackTest, RejectsNullPacket)
{
	EXPECT_FALSE(RecordPacket(true, nullptr));
	EXPECT_FALSE(HasRecord(10));
}
