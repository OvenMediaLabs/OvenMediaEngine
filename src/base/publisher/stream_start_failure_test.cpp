//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2026 OvenMediaLabs. All rights reserved.
//
//==============================================================================
#include <base/publisher/application.h>
#include <base/publisher/publisher.h>
#include <base/publisher/stream.h>
#include <gtest/gtest.h>

#include <any>
#include <chrono>
#include <filesystem>
#include <optional>
#include <thread>
#include <utility>

namespace
{
	constexpr uint32_t WORKER_COUNT = 4;

	class TestApplicationInfo : public info::Application
	{
	public:
		using info::Application::GetInvalidApplication;
	};

	class TestPublisher : public pub::Publisher
	{
	public:
		TestPublisher()
			: pub::Publisher(cfg::Server(), std::shared_ptr<MediaRouterInterface>())
		{
		}

		PublisherType GetPublisherType() const override
		{
			return PublisherType::Webrtc;
		}

		const char *GetPublisherName() const override
		{
			return "TestPublisher";
		}

		ocst::ModuleType GetModuleType() const override
		{
			return ocst::ModuleType::Publisher;
		}

		bool OnCreateHost(const info::Host &host_info) override
		{
			return true;
		}

		bool OnDeleteHost(const info::Host &host_info) override
		{
			return true;
		}

	private:
		std::shared_ptr<pub::Application> OnCreatePublisherApplication(const info::Application &application_info) override
		{
			return nullptr;
		}

		bool OnDeletePublisherApplication(const std::shared_ptr<pub::Application> &application) override
		{
			return true;
		}
	};

	// Starts its workers and adds a session like the HLS origin session pool,
	// then fails as `LLHlsStream::Start()` does for a stream with no supported codec
	class FailingStartStream : public pub::Stream
	{
	public:
		FailingStartStream(const std::shared_ptr<pub::Application> &application, const info::Stream &info)
			: pub::Stream(application, info)
		{
		}

		void SendVideoFrame(const std::shared_ptr<MediaPacket> &media_packet) override {}
		void SendAudioFrame(const std::shared_ptr<MediaPacket> &media_packet) override {}
		void SendDataFrame(const std::shared_ptr<MediaPacket> &media_packet) override {}

		std::shared_ptr<pub::Session> MakeSession()
		{
			return std::make_shared<pub::Session>(info::Session(*this, IssueUniqueSessionId()), GetApplication(), GetSharedPtr());
		}

		// The session holds this stream, so the test takes it out before checking that the stream is released
		std::shared_ptr<pub::Session> TakeStartSession()
		{
			return std::exchange(_start_session, nullptr);
		}

	protected:
		bool Start() override
		{
			EXPECT_TRUE(CreateStreamWorker(WORKER_COUNT));

			_start_session = MakeSession();
			EXPECT_TRUE(AddSession(_start_session));

			return false;
		}

	private:
		std::shared_ptr<pub::Session> _start_session;
	};

	class TestApplication : public pub::Application
	{
	public:
		explicit TestApplication(const std::shared_ptr<pub::Publisher> &publisher)
			: pub::Application(publisher, TestApplicationInfo::GetInvalidApplication())
		{
		}

		std::weak_ptr<pub::Stream> GetLastCreatedStream() const
		{
			return _last_created_stream;
		}

	private:
		std::shared_ptr<pub::Stream> CreateStream(const std::shared_ptr<info::Stream> &info, uint32_t thread_count) override
		{
			auto stream			 = std::make_shared<FailingStartStream>(GetSharedPtrAs<pub::Application>(), *info);
			_last_created_stream = stream;
			return stream;
		}

		bool DeleteStream(const std::shared_ptr<info::Stream> &info) override
		{
			return true;
		}

		std::weak_ptr<pub::Stream> _last_created_stream;
	};

	// Nullopt without a readable `/proc`, so the caller can skip instead of passing silently
	std::optional<size_t> CountThreads()
	{
		std::error_code error;
		std::filesystem::directory_iterator entries("/proc/self/task", error);
		if (error)
		{
			return std::nullopt;
		}

		size_t count = 0;

		for ([[maybe_unused]] const auto &entry : entries)
		{
			count++;
		}

		return count;
	}

	// A joined thread leaves `/proc/self/task` slightly after `join()` returns, so the count is polled
	std::optional<size_t> WaitForThreadCount(size_t expected)
	{
		constexpr auto TIMEOUT = std::chrono::seconds(1);

		auto deadline		   = std::chrono::steady_clock::now() + TIMEOUT;
		auto count			   = CountThreads();

		while ((count != expected) && (std::chrono::steady_clock::now() < deadline))
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
			count = CountThreads();
		}

		return count;
	}
}  // namespace

// A stream whose `Start()` fails after starting its workers stops its sessions, refuses new sessions and messages,
// and is released with its worker threads once it is deleted
TEST(PublisherStreamStartFailureTest, FailedStartReleasesWorkersAndSessions)
{
	auto application = std::make_shared<TestApplication>(std::make_shared<TestPublisher>());
	ASSERT_TRUE(application->Start());

	auto threads_before = CountThreads();
	if (threads_before.has_value() == false)
	{
		GTEST_SKIP() << "/proc/self/task is not readable";
	}

	auto info = std::make_shared<info::Stream>(*application, StreamSourceType::Ovt);
	info->SetName("start_failure");
	// Like every publisher stream in production, it is linked to its input stream
	info->LinkInputStream(std::make_shared<info::Stream>(*application, StreamSourceType::Ovt));

	ASSERT_TRUE(application->OnStreamCreated(info));
	auto stream = application->GetLastCreatedStream();
	ASSERT_FALSE(stream.expired());

	EXPECT_FALSE(application->OnStreamPrepared(info));

	{
		auto failed_stream = std::static_pointer_cast<FailingStartStream>(stream.lock());
		EXPECT_EQ(failed_stream->GetState(), pub::Stream::State::ERROR);

		auto start_session = failed_stream->TakeStartSession();
		ASSERT_NE(start_session, nullptr);
		EXPECT_EQ(start_session->GetState(), pub::Session::SessionState::Stopped);
		EXPECT_EQ(failed_stream->GetSessionCount(), 0u);

		EXPECT_FALSE(failed_stream->AddSession(failed_stream->MakeSession()));
		EXPECT_FALSE(failed_stream->SendMessage(start_session, std::any()));
	}

	ASSERT_TRUE(application->OnStreamDeleted(info));

	EXPECT_TRUE(stream.expired());
	EXPECT_EQ(WaitForThreadCount(threads_before.value()), threads_before);

	application->Stop();
}
