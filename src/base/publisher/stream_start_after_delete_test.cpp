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

#include <filesystem>
#include <optional>

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

	// Starts like the publisher streams do: refuses unless it is still created, then starts its workers
	class WorkerStream : public pub::Stream
	{
	public:
		WorkerStream(const std::shared_ptr<pub::Application> &application, const info::Stream &info)
			: pub::Stream(application, info)
		{
		}

		void SendVideoFrame(const std::shared_ptr<MediaPacket> &media_packet) override {}
		void SendAudioFrame(const std::shared_ptr<MediaPacket> &media_packet) override {}
		void SendDataFrame(const std::shared_ptr<MediaPacket> &media_packet) override {}

	protected:
		bool Start() override
		{
			if (GetState() != State::CREATED)
			{
				return false;
			}

			if (CreateStreamWorker(WORKER_COUNT) == false)
			{
				return false;
			}

			return pub::Stream::Start();
		}
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
			auto stream			 = std::make_shared<WorkerStream>(GetSharedPtrAs<pub::Application>(), *info);
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
}  // namespace

// `OnStreamPrepared()` releases the stream map lock before it calls `EnterStart()`,
// so a delete can remove and stop the stream in between.
// The start that comes after the delete must not bring the removed stream back with its workers.
TEST(PublisherStreamStartAfterDeleteTest, StartAfterDeleteIsRefused)
{
	auto application = std::make_shared<TestApplication>(std::make_shared<TestPublisher>());
	ASSERT_TRUE(application->Start());

	auto threads_before = CountThreads();
	if (threads_before.has_value() == false)
	{
		GTEST_SKIP() << "/proc/self/task is not readable";
	}

	auto info = std::make_shared<info::Stream>(*application, StreamSourceType::Ovt);
	info->SetName("start_after_delete");

	ASSERT_TRUE(application->OnStreamCreated(info));
	auto weak_stream = application->GetLastCreatedStream();

	// Held like the copy `OnStreamPrepared()` takes from the stream map before it releases the lock
	auto stream		 = weak_stream.lock();
	ASSERT_NE(stream, nullptr);

	ASSERT_TRUE(application->OnStreamDeleted(info));
	EXPECT_EQ(stream->GetState(), pub::Stream::State::STOPPED);

	EXPECT_FALSE(stream->EnterStart());
	EXPECT_EQ(stream->GetState(), pub::Stream::State::STOPPED);

	// The refused start creates no worker, so no thread is left to join
	EXPECT_EQ(CountThreads(), threads_before);

	stream.reset();
	EXPECT_TRUE(weak_stream.expired());

	application->Stop();
}
