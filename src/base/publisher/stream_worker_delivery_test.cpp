//==============================================================================
//
//  OvenMediaEngine
//
//  Copyright (c) 2026 OvenMediaLabs. All rights reserved.
//
//==============================================================================
#include <base/publisher/application.h>
#include <base/publisher/publisher.h>
#include <base/publisher/session.h>
#include <base/publisher/stream.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <any>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <vector>

namespace
{
	// A session belongs to the worker at `session id % WORKER_COUNT`
	constexpr uint32_t WORKER_COUNT		 = 2;
	constexpr session_id_t BUSY_SESSION	 = 2;
	constexpr session_id_t FIRST_SESSION = 1;
	constexpr session_id_t AGAIN_SESSION = 3;

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

	class TestApplication : public pub::Application
	{
	public:
		explicit TestApplication(const std::shared_ptr<pub::Publisher> &publisher)
			: pub::Application(publisher, TestApplicationInfo::GetInvalidApplication())
		{
		}

	private:
		std::shared_ptr<pub::Stream> CreateStream(const std::shared_ptr<info::Stream> &info, uint32_t thread_count) override
		{
			return nullptr;
		}

		bool DeleteStream(const std::shared_ptr<info::Stream> &info) override
		{
			return true;
		}
	};

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
			if (CreateStreamWorker(WORKER_COUNT) == false)
			{
				return false;
			}

			return pub::Stream::Start();
		}
	};

	// Records the packets its worker thread delivers
	class RecordingSession : public pub::Session
	{
	public:
		RecordingSession(const std::shared_ptr<pub::Stream> &stream, session_id_t session_id)
			: pub::Session(info::Session(*stream, session_id), stream->GetApplication(), stream)
		{
		}

		void SendOutgoingData(const std::any &packet) override
		{
			std::lock_guard<std::mutex> lock(_mutex);
			_received.push_back(std::any_cast<int>(packet));
			_condition.notify_all();
		}

		// Blocks until the packet arrives, the timeout only ends a failing run
		bool WaitFor(int packet)
		{
			constexpr auto TIMEOUT = std::chrono::seconds(10);

			std::unique_lock<std::mutex> lock(_mutex);
			return _condition.wait_for(lock, TIMEOUT, [&]() {
				return std::find(_received.begin(), _received.end(), packet) != _received.end();
			});
		}

		std::vector<int> Received() const
		{
			std::lock_guard<std::mutex> lock(_mutex);
			return _received;
		}

	private:
		mutable std::mutex _mutex;
		std::condition_variable _condition;
		std::vector<int> _received;
	};
}  // namespace

// A worker receives packets only while it holds a session:
// its first session, the loss of its last session and a session added again each take effect with the next packet
TEST(PublisherStreamWorkerDeliveryTest, DeliversToAWorkerOnlyWhileItHoldsASession)
{
	auto application = std::make_shared<TestApplication>(std::make_shared<TestPublisher>());
	ASSERT_TRUE(application->Start());

	info::Stream stream_info(*application, StreamSourceType::Ovt);
	stream_info.SetName("worker_delivery");
	// Like every publisher stream in production, it is linked to its input stream
	stream_info.LinkInputStream(std::make_shared<info::Stream>(*application, StreamSourceType::Ovt));

	auto stream = std::make_shared<WorkerStream>(application, stream_info);
	ASSERT_TRUE(stream->EnterStart());
	EXPECT_EQ(stream->GetSessionCount(), 0u);

	// No session yet, so nobody receives this
	EXPECT_TRUE(stream->BroadcastPacket(std::make_any<int>(1)));

	// One worker stays occupied for the whole test
	auto busy = std::make_shared<RecordingSession>(stream, BUSY_SESSION);
	ASSERT_TRUE(stream->AddSession(busy));
	EXPECT_EQ(stream->GetSessionCount(), 1u);

	EXPECT_TRUE(stream->BroadcastPacket(std::make_any<int>(10)));
	ASSERT_TRUE(busy->WaitFor(10));

	// The other worker gains its first session
	auto first = std::make_shared<RecordingSession>(stream, FIRST_SESSION);
	ASSERT_TRUE(stream->AddSession(first));
	EXPECT_EQ(stream->GetSessionCount(), 2u);

	EXPECT_TRUE(stream->BroadcastPacket(std::make_any<int>(20)));
	ASSERT_TRUE(busy->WaitFor(20));
	ASSERT_TRUE(first->WaitFor(20));

	// It loses its last session
	ASSERT_TRUE(stream->RemoveSession(FIRST_SESSION));
	EXPECT_EQ(stream->GetSessionCount(), 1u);

	EXPECT_TRUE(stream->BroadcastPacket(std::make_any<int>(30)));
	ASSERT_TRUE(busy->WaitFor(30));

	// It gains a session again
	auto again = std::make_shared<RecordingSession>(stream, AGAIN_SESSION);
	ASSERT_TRUE(stream->AddSession(again));
	EXPECT_EQ(stream->GetSessionCount(), 2u);

	EXPECT_TRUE(stream->BroadcastPacket(std::make_any<int>(40)));
	ASSERT_TRUE(busy->WaitFor(40));
	ASSERT_TRUE(again->WaitFor(40));

	EXPECT_EQ(busy->Received(), (std::vector<int>{10, 20, 30, 40}));
	EXPECT_EQ(first->Received(), (std::vector<int>{20}));
	// The packet sent while its worker held no session was never queued for it
	EXPECT_EQ(again->Received(), (std::vector<int>{40}));

	EXPECT_TRUE(stream->EnterStop(pub::Stream::StopCause::StreamDeleted));
	EXPECT_EQ(stream->GetSessionCount(), 0u);

	application->Stop();
}
