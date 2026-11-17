//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2026 OvenMediaLabs. All rights reserved.
//
//==============================================================================
#include "rtc_stream.h"

#include <base/info/application.h>
#include <gtest/gtest.h>

#include "rtc_application.h"

namespace
{
	class TestApplicationInfo : public info::Application
	{
	public:
		using info::Application::GetInvalidApplication;
	};
}  // namespace

// Friended by RtcStream; TEST_F bodies go through its helpers.
class RtcStreamStopTest : public ::testing::Test
{
protected:
	static size_t AddVideoPacketizer(const std::shared_ptr<RtcStream> &stream)
	{
		auto video = std::make_shared<MediaTrack>();
		video->SetId(1);
		video->SetMediaType(cmn::MediaType::Video);
		video->SetCodecId(cmn::MediaCodecId::Vp8);

		stream->_video_ssrc = 1000;
		stream->AddPacketizer(video);

		return stream->_packetizers.size();
	}
};

// A stream that never started is released once it is stopped, with the packetizers it holds.
// `RtcStream::Start()` stops itself this way on its no-playlist failure, which a test config cannot select
TEST_F(RtcStreamStopTest, StopReleasesPacketizersOfStreamThatNeverStarted)
{
	auto app	= std::make_shared<RtcApplication>(nullptr, TestApplicationInfo::GetInvalidApplication(), nullptr, nullptr);
	auto stream = RtcStream::Create(app, info::Stream(*app, StreamSourceType::Ovt), 0);
	ASSERT_NE(stream, nullptr);
	ASSERT_EQ(AddVideoPacketizer(stream), 1u);

	std::weak_ptr<RtcStream> weak_stream = stream;

	// Never started, so `Stop()` reports that there was nothing to stop
	EXPECT_FALSE(stream->EnterStop(pub::Stream::StopCause::StreamDeleted));
	stream.reset();

	EXPECT_TRUE(weak_stream.expired());
}
