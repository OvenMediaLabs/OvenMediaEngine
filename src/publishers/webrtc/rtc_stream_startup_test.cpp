//==============================================================================
//
//  OvenMediaEngine
//
//  Copyright (c) 2026 OvenMediaLabs. All rights reserved.
//
//==============================================================================
#include <gtest/gtest.h>

#include <base/info/application.h>
#include <base/info/media_track.h>
#include <base/publisher/session.h>
#include <base/publisher/stream.h>
#include <vector>

#include "rtc_application.h"

namespace
{
	using cmn::MediaType;
	using cmn::MediaCodecId;

	class TestApplicationInfo : public info::Application
	{
	public:
		using info::Application::GetInvalidApplication;
	};

	class RecordingSession : public pub::Session
	{
	public:
		explicit RecordingSession(const std::shared_ptr<RtcStream> &stream)
			: pub::Session(info::Session(*stream, 1), stream->GetApplication(), stream)
		{
		}

		void SendOutgoingData(const std::any &packet) override
		{
			packets.push_back(std::any_cast<std::shared_ptr<RtpPacket>>(packet));
		}

		std::vector<std::shared_ptr<RtpPacket>> packets;
	};

	class RtcStreamStartupTest : public ::testing::Test
	{
	protected:
		void SetUp() override
		{
			auto app = std::make_shared<RtcApplication>(nullptr, TestApplicationInfo::GetInvalidApplication(), nullptr, nullptr);
			// No workers: BroadcastPacket delivers synchronously to the recording session.
			stream = RtcStream::Create(app, info::Stream(*app, StreamSourceType::Ovt), 0);
			auto video = std::make_shared<MediaTrack>();
			video->SetId(1);
			video->SetMediaType(MediaType::Video);
			video->SetCodecId(MediaCodecId::Vp8);
			video->SetVariantName("video");
			video->SetTimeBase(1, 90000);
			ASSERT_TRUE(stream->AddTrack(video));
			auto audio = std::make_shared<MediaTrack>();
			audio->SetId(2);
			audio->SetMediaType(MediaType::Audio);
			audio->SetCodecId(MediaCodecId::Opus);
			audio->SetVariantName("audio");
			audio->SetTimeBase(1, 48000);
			audio->SetSampleRate(48000);
			audio->SetChannelLayout(cmn::AudioChannel::Layout::LayoutStereo);
			ASSERT_TRUE(stream->AddTrack(audio));
			ASSERT_EQ(stream->GetState(), pub::Stream::State::CREATED);
			ASSERT_EQ(stream->GetSessionCount(), 0u);
		}

		void TearDown() override
		{
			// Stop explicitly to release the stream/session and stream/packetizer cycles.
			stream->EnterStop(pub::Stream::StopCause::ApplicationStopped);
		}

		std::shared_ptr<MediaPacket> VideoFrame(int64_t pts, MediaPacketFlag flag)
		{
			uint8_t data[] = {0x10, 0x00, 0x00, 0x9d, 0x01, 0x2a, 0x10, 0x00, 0x10, 0x00};
			if (flag != MediaPacketFlag::Key)
			{
				data[0] |= 1;
			}
			return std::make_shared<MediaPacket>(MediaType::Video, 1, data, sizeof(data), pts, pts, 3000,
				flag, cmn::BitstreamFormat::VP8, cmn::PacketType::RAW);
		}

		std::shared_ptr<MediaPacket> AudioFrame(int64_t pts)
		{
			const uint8_t data[] = {0xf8, 0xff, 0xfe};
			return std::make_shared<MediaPacket>(MediaType::Audio, 2, data, sizeof(data), pts, pts, 960,
				MediaPacketFlag::Key, cmn::BitstreamFormat::OPUS, cmn::PacketType::RAW);
		}

		std::shared_ptr<RtcStream> stream;
	};
}  // namespace

TEST_F(RtcStreamStartupTest, DeliversBufferedKeyframeBeforeFirstDelta)
{
	stream->SendVideoFrame(VideoFrame(0, MediaPacketFlag::Key));
	ASSERT_TRUE(stream->EnterStart());
	ASSERT_EQ(stream->GetState(), pub::Stream::State::STARTED);

	auto session = std::make_shared<RecordingSession>(stream);
	ASSERT_TRUE(stream->AddSession(session));
	stream->SendVideoFrame(VideoFrame(3000, MediaPacketFlag::NoFlag));

	ASSERT_EQ(session->packets.size(), 2u);
	EXPECT_TRUE(session->packets[0]->IsKeyframe());
	EXPECT_EQ(session->packets[0]->Timestamp(), 0u);
	EXPECT_FALSE(session->packets[1]->IsKeyframe());
	EXPECT_EQ(session->packets[1]->Timestamp(), 3000u);
}

TEST_F(RtcStreamStartupTest, DropsBufferedVideoAfterASkippedFrame)
{
	stream->SendVideoFrame(VideoFrame(0, MediaPacketFlag::Key));
	ASSERT_TRUE(stream->EnterStart());
	// Skipped with no session, so the buffered keyframe no longer leads into the next frame
	stream->SendVideoFrame(VideoFrame(3000, MediaPacketFlag::NoFlag));

	auto session = std::make_shared<RecordingSession>(stream);
	ASSERT_TRUE(stream->AddSession(session));
	stream->SendVideoFrame(VideoFrame(6000, MediaPacketFlag::NoFlag));

	ASSERT_EQ(session->packets.size(), 1u);
	EXPECT_EQ(session->packets[0]->Timestamp(), 6000u);
}

TEST_F(RtcStreamStartupTest, DropsBufferedAudioAfterASkippedFrame)
{
	stream->SendAudioFrame(AudioFrame(0));
	ASSERT_TRUE(stream->EnterStart());
	// Skipped with no session, so the buffered audio would arrive out of date
	stream->SendAudioFrame(AudioFrame(960));

	auto session = std::make_shared<RecordingSession>(stream);
	ASSERT_TRUE(stream->AddSession(session));
	stream->SendAudioFrame(AudioFrame(1920));

	ASSERT_EQ(session->packets.size(), 1u);
	EXPECT_EQ(session->packets[0]->Timestamp(), 1920u);
}

TEST_F(RtcStreamStartupTest, DeliversBufferedAudioWhenNothingWasSkipped)
{
	stream->SendAudioFrame(AudioFrame(0));
	ASSERT_TRUE(stream->EnterStart());

	auto session = std::make_shared<RecordingSession>(stream);
	ASSERT_TRUE(stream->AddSession(session));
	stream->SendAudioFrame(AudioFrame(960));

	ASSERT_EQ(session->packets.size(), 2u);
	EXPECT_EQ(session->packets[0]->Timestamp(), 0u);
	EXPECT_EQ(session->packets[1]->Timestamp(), 960u);
}
