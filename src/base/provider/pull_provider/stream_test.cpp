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
