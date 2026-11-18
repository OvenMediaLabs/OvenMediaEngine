//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2026 OvenMediaLabs. All rights reserved.
//
//==============================================================================
#include <gtest/gtest.h>

#include <atomic>
#include <mutex>
#include <set>
#include <thread>

#include "stream.h"

namespace
{
	class TestApplicationInfo : public info::Application
	{
	public:
		using info::Application::GetInvalidApplication;
	};

	// A session reads its ids from the stream's application info, so the stream carries one.
	// Like every publisher stream in production, it is linked to its input stream.
	info::Stream MakeOutputStreamInfo()
	{
		const auto &app_info = TestApplicationInfo::GetInvalidApplication();

		info::Stream stream_info(app_info, StreamSourceType::Ovt);
		stream_info.LinkInputStream(std::make_shared<info::Stream>(app_info, StreamSourceType::Ovt));

		return stream_info;
	}

	class StopHookStream : public pub::Stream
	{
	public:
		StopHookStream()
			: pub::Stream(nullptr, MakeOutputStreamInfo())
		{
		}

		void SendVideoFrame(const std::shared_ptr<MediaPacket> &media_packet) override {}
		void SendAudioFrame(const std::shared_ptr<MediaPacket> &media_packet) override {}
		void SendDataFrame(const std::shared_ptr<MediaPacket> &media_packet) override {}

		std::set<session_id_t> StoppingSessionIds() const
		{
			std::lock_guard<std::mutex> lock(_mutex);
			return _stopping_session_ids;
		}

		int StoppingCount() const
		{
			std::lock_guard<std::mutex> lock(_mutex);
			return _stopping_count;
		}

	protected:
		void OnStopping(const std::map<session_id_t, std::shared_ptr<pub::Session>> &sessions) override
		{
			std::lock_guard<std::mutex> lock(_mutex);

			_stopping_count++;
			for (const auto &[session_id, session] : sessions)
			{
				_stopping_session_ids.insert(session_id);
			}
		}

	private:
		mutable std::mutex _mutex;
		std::set<session_id_t> _stopping_session_ids;
		int _stopping_count = 0;
	};

	std::shared_ptr<StopHookStream> MakeStartedStream()
	{
		auto stream = std::make_shared<StopHookStream>();

		// No worker thread, so the test needs no application
		EXPECT_TRUE(stream->CreateStreamWorker(0));
		EXPECT_TRUE(stream->EnterStart());

		return stream;
	}

	std::shared_ptr<pub::Session> MakeSession(const std::shared_ptr<pub::Stream> &stream, session_id_t session_id)
	{
		return std::make_shared<pub::Session>(info::Session(*stream, session_id), nullptr, stream);
	}
}  // namespace

// `Stop()` hands the hook every session it is about to stop
TEST(PublisherStreamStopTest, StoppingHookReceivesEverySession)
{
	auto stream = MakeStartedStream();

	ASSERT_TRUE(stream->AddSession(MakeSession(stream, 1)));
	ASSERT_TRUE(stream->AddSession(MakeSession(stream, 2)));

	ASSERT_TRUE(stream->EnterStop(pub::Stream::StopCause::StreamDeleted));

	EXPECT_EQ(stream->StoppingCount(), 1);
	EXPECT_EQ(stream->StoppingSessionIds(), (std::set<session_id_t>{1, 2}));

	// A session after the stop is refused, so no accepted session misses the hook
	EXPECT_FALSE(stream->AddSession(MakeSession(stream, 3)));
	EXPECT_EQ(stream->StoppingCount(), 1);
}

// A session accepted while `Stop()` runs on another thread is either refused or handed to the hook, never neither
TEST(PublisherStreamStopTest, SessionAddedDuringStopIsHandedToTheHookOrRefused)
{
	constexpr int ITERATIONS		 = 200;
	constexpr int SESSIONS_PER_ROUND = 64;

	for (int iteration = 0; iteration < ITERATIONS; iteration++)
	{
		auto stream = MakeStartedStream();

		std::atomic<bool> go{false};
		std::set<session_id_t> accepted;

		std::thread adder([&]() {
			while (go.load() == false)
			{
				std::this_thread::yield();
			}

			for (session_id_t id = 1; id <= SESSIONS_PER_ROUND; id++)
			{
				if (stream->AddSession(MakeSession(stream, id)))
				{
					accepted.insert(id);
				}
			}
		});

		go.store(true);
		ASSERT_TRUE(stream->EnterStop(pub::Stream::StopCause::StreamDeleted));
		adder.join();

		ASSERT_EQ(stream->StoppingSessionIds(), accepted) << "iteration " << iteration;
	}
}
