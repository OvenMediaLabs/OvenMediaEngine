//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2026 OvenMediaLabs. All rights reserved.
//
//==============================================================================
#include <base/provider/pull_provider/stream.h>
#include <base/provider/pull_provider/stream_props.h>
#include <gtest/gtest.h>

// Tests for the `PullStream` empty-URL guard on this branch:
// when every configured URL fails to parse (reachable through the `OriginMap` pull path,
// which performs no URL validation), `GetNextURL()` returns `nullptr`
// and `Start()`/`Resume()` must terminate the stream cleanly
// instead of feeding the `nullptr` into provider `StartStream()`/`RestartStream()`
// implementations that dereference it unconditionally
// (RTSPC crashed on this before the fix).
//
// Note: the failure paths under test never touch the (`nullptr`) application pointer -
// only the success path logs through it,
// which is why these tests stick to the empty/unparsable cases.

namespace
{
	std::shared_ptr<pvd::PullStreamProperties> RetryingProperties()
	{
		// The default retry count (-1) makes `ResumeInternal()` bail out
		// at the retry-count gate BEFORE reaching the null-URL guard under test
		auto properties = std::make_shared<pvd::PullStreamProperties>();
		properties->SetRetryCount(1);
		return properties;
	}

	class TestPullStream : public pvd::PullStream
	{
	public:
		explicit TestPullStream(const std::vector<ov::String> &url_list)
			: pvd::PullStream(std::shared_ptr<pvd::Application>(), info::Stream(StreamSourceType::Ovt), url_list, RetryingProperties())
		{
		}

		int start_stream_calls = 0;
		int restart_stream_calls = 0;

		ProcessMediaEventTrigger GetProcessMediaEventTriggerMode() override
		{
			return ProcessMediaEventTrigger::TRIGGER_EPOLL;
		}

		int GetFileDescriptorForDetectingEvent() override
		{
			return -1;
		}

		ProcessMediaResult ProcessMediaPacket() override
		{
			return ProcessMediaResult::PROCESS_MEDIA_FINISH;
		}

	protected:
		bool StartStream(const std::shared_ptr<const ov::Url> &url) override
		{
			start_stream_calls++;
			// The guard under test must prevent a `nullptr` from ever arriving here
			EXPECT_NE(url, nullptr);
			return false;
		}

		bool RestartStream(const std::shared_ptr<const ov::Url> &url) override
		{
			restart_stream_calls++;
			EXPECT_NE(url, nullptr);
			return false;
		}

		bool StopStream() override
		{
			return true;
		}
	};
}  // namespace

TEST(PullStreamEmptyUrl, StartTerminatesWithoutCallingProvider)
{
	TestPullStream stream({});

	EXPECT_FALSE(stream.Start());
	EXPECT_EQ(stream.GetState(), pvd::Stream::State::TERMINATED);
	EXPECT_EQ(stream.start_stream_calls, 0);
}

TEST(PullStreamEmptyUrl, UnparsableUrlsTerminateWithoutCallingProvider)
{
	// Every entry fails `ov::Url::Parse()`, so the constructor leaves the internal
	// URL list empty - exactly what an invalid Origins entry produces.
	// Pin the precondition: if either string ever starts parsing,
	// this test would silently stop covering the empty-list path.
	ASSERT_EQ(ov::Url::Parse("definitely not a url"), nullptr);
	ASSERT_EQ(ov::Url::Parse("also::not::valid"), nullptr);

	TestPullStream stream({"definitely not a url", "also::not::valid"});

	EXPECT_FALSE(stream.Start());
	EXPECT_EQ(stream.GetState(), pvd::Stream::State::TERMINATED);
	EXPECT_EQ(stream.start_stream_calls, 0);
}

TEST(PullStreamEmptyUrl, ResumeTerminatesWithoutCallingProvider)
{
	TestPullStream stream({});

	EXPECT_FALSE(stream.Resume());
	EXPECT_EQ(stream.GetState(), pvd::Stream::State::TERMINATED);
	EXPECT_EQ(stream.restart_stream_calls, 0);
}

// Tests for the retry budget. A handshake that succeeds says only that the origin answered;
// a session that then delivers nothing has made no progress, and counting it as progress would
// let a deterministic mid-stream refusal (an OVT `required` token this build does not know, for one)
// reconnect without end.
namespace
{
	class SilentOriginPullStream : public pvd::PullStream
	{
	public:
		SilentOriginPullStream(const std::vector<ov::String> &url_list, int32_t retry_count)
			: pvd::PullStream(std::shared_ptr<pvd::Application>(), info::Stream(StreamSourceType::Ovt), url_list,
							  [retry_count] {
								  auto properties = std::make_shared<pvd::PullStreamProperties>();
								  properties->SetRetryCount(retry_count);
								  return properties;
							  }())
		{
		}

		int restart_stream_calls = 0;

		// Stands in for a media packet arriving from the origin
		void DeliverOriginMedia()
		{
			SendFrame(std::make_shared<MediaPacket>(cmn::MediaType::Video, 0,
													std::make_shared<ov::Data>(),
													0, 0, 0, MediaPacketFlag::Key,
													cmn::BitstreamFormat::H264_ANNEXB,
													cmn::PacketType::NALU));
		}

		// Stands in for a packet this stream made for itself
		void SendInternalPacket(const std::shared_ptr<MediaPacket> &packet)
		{
			SendFrame(packet);
		}

		ProcessMediaEventTrigger GetProcessMediaEventTriggerMode() override
		{
			return ProcessMediaEventTrigger::TRIGGER_EPOLL;
		}

		int GetFileDescriptorForDetectingEvent() override
		{
			return -1;
		}

		ProcessMediaResult ProcessMediaPacket() override
		{
			return ProcessMediaResult::PROCESS_MEDIA_TRY_AGAIN;
		}

	protected:
		bool StartStream(const std::shared_ptr<const ov::Url> &url) override
		{
			return url != nullptr;
		}

		// The origin answers every time; it just never sends media
		bool RestartStream(const std::shared_ptr<const ov::Url> &url) override
		{
			restart_stream_calls++;
			return url != nullptr;
		}

		bool StopStream() override
		{
			return true;
		}
	};
}  // namespace

TEST(PullStreamRetryBudget, SucceedingHandshakeWithoutMediaIsBounded)
{
	// Two URLs and one retry each: three attempts exhaust the budget
	SilentOriginPullStream stream({"ovt://origin1:9000/app/stream", "ovt://origin2:9000/app/stream"}, 1);

	EXPECT_TRUE(stream.Resume());
	EXPECT_TRUE(stream.Resume());

	EXPECT_FALSE(stream.Resume());
	EXPECT_EQ(stream.GetState(), pvd::Stream::State::TERMINATED);
	EXPECT_EQ(stream.restart_stream_calls, 2);
}

TEST(PullStreamRetryBudget, MediaFromTheOriginRefillsTheBudget)
{
	SilentOriginPullStream stream({"ovt://origin1:9000/app/stream", "ovt://origin2:9000/app/stream"}, 1);

	EXPECT_TRUE(stream.Resume());
	EXPECT_TRUE(stream.Resume());

	// The session that is ending delivered media, so the next resume starts from a full budget
	// instead of the third and last attempt
	stream.DeliverOriginMedia();

	EXPECT_TRUE(stream.Resume());
	EXPECT_TRUE(stream.Resume());
	EXPECT_TRUE(stream.Resume());

	EXPECT_FALSE(stream.Resume());
	EXPECT_EQ(stream.GetState(), pvd::Stream::State::TERMINATED);
}

TEST(PullStreamRetryBudget, InternallyCreatedPacketsDoNotRefillTheBudget)
{
	// An event or a subtitle this stream made for itself says nothing about the origin.
	// `EventGenerator` emits on a schedule of its own, so counting one as origin media would
	// hold a dead origin open for as long as the schedule runs.
	SilentOriginPullStream stream({"ovt://origin1:9000/app/stream"}, 1);

	auto event = std::make_shared<MediaPacket>(cmn::MediaType::Data, 0,
											   std::make_shared<ov::Data>(),
											   0, 0, 0, MediaPacketFlag::NoFlag,
											   cmn::BitstreamFormat::OVEN_EVENT,
											   cmn::PacketType::EVENT);
	event->SetInternalCreated(true);

	EXPECT_TRUE(stream.Resume());
	stream.SendInternalPacket(event);

	EXPECT_FALSE(stream.Resume());
	EXPECT_EQ(stream.GetState(), pvd::Stream::State::TERMINATED);
}
