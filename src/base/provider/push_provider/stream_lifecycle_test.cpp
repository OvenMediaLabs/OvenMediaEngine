//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2026 OvenMediaLabs. All rights reserved.
//
//==============================================================================
#include <base/mediarouter/mediarouter_interface.h>
#include <config/config.h>
#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <functional>
#include <future>
#include <thread>

#include "application.h"
#include "provider.h"
#include "stream.h"

//  Covers the channel state the channel task runner reads before it deletes a channel:
//  the silence budget, the last received time, and the mark that says a handler is running.
//
//  These are what decide whether a live input survives, and the runner reads them from another
//  thread, so a wrong transition shows up as a deleted stream rather than as a failure here.
namespace
{
	class StubPushApplication;

	class StubPushProvider : public pvd::PushProvider
	{
	public:
		StubPushProvider(const cfg::Server &server_config, const std::shared_ptr<MediaRouterInterface> &router)
			: PushProvider(server_config, router)
		{
		}

		ProviderType GetProviderType() const override
		{
			return ProviderType::Rtmp;
		}

		ProviderStreamDirection GetProviderStreamDirection() const override
		{
			return ProviderStreamDirection::Push;
		}

		const char *GetProviderName() const override
		{
			return "StubPushProvider";
		}

		bool OnCreateHost(const info::Host &host_info) override
		{
			return true;
		}

		bool OnDeleteHost(const info::Host &host_info) override
		{
			return true;
		}

		std::shared_ptr<pvd::Application> OnCreateProviderApplication(const info::Application &app_info) override;

		bool OnDeleteProviderApplication(const std::shared_ptr<pvd::Application> &application) override
		{
			return true;
		}

		// Concrete providers reach these from their own socket callbacks, where they are protected.
		using PushProvider::OnChannelCreated;
		using PushProvider::OnDataReceived;
		// The orchestrator calls this one.
		using PushProvider::OnCreateApplication;
	};

	// The provider registers its applications with the router before storing them, so a channel
	// cannot reach its application without one.
	class StubMediaRouter : public MediaRouterInterface
	{
	public:
		CommonErrorCode MirrorStream(std::shared_ptr<MediaRouterStreamTap> &stream_tap, const info::VHostAppName &vhost_app_name,
									 const ov::String &stream_name, MirrorPosition posision) override
		{
			return CommonErrorCode::SUCCESS;
		}

		CommonErrorCode UnmirrorStream(const std::shared_ptr<MediaRouterStreamTap> &stream_tap) override
		{
			return CommonErrorCode::SUCCESS;
		}

		bool RegisterConnectorApp(const info::Application &application_info,
								  const std::shared_ptr<MediaRouterApplicationConnector> &application_connector) override
		{
			return true;
		}

		bool UnregisterConnectorApp(const info::Application &application_info,
									const std::shared_ptr<MediaRouterApplicationConnector> &application_connector) override
		{
			return true;
		}

		bool RegisterObserverApp(const info::Application &application_info,
								 const std::shared_ptr<MediaRouterApplicationObserver> &application_observer) override
		{
			return true;
		}

		bool UnregisterObserverApp(const info::Application &application_info,
								   const std::shared_ptr<MediaRouterApplicationObserver> &application_observer) override
		{
			return true;
		}
	};

	// Both constructors are reachable only from a subclass, because normally the orchestrator and the
	// concrete providers are the ones that build these.
	class TestApplicationInfo : public info::Application
	{
	public:
		TestApplicationInfo(const info::Host &host_info, const info::VHostAppName &vhost_app_name,
							const cfg::vhost::app::Application &app_config)
			: info::Application(host_info, 1, vhost_app_name, app_config, false)
		{
		}
	};

	class StubPushApplication : public pvd::PushApplication
	{
	public:
		StubPushApplication(const std::shared_ptr<pvd::PushProvider> &provider, const info::Application &application_info)
			: PushApplication(provider, application_info)
		{
		}
	};

	std::shared_ptr<pvd::Application> StubPushProvider::OnCreateProviderApplication(const info::Application &app_info)
	{
		return std::make_shared<StubPushApplication>(GetSharedPtrAs<pvd::PushProvider>(), app_info);
	}

	class StubPushStream : public pvd::PushStream
	{
	public:
		StubPushStream(uint32_t channel_id, const std::shared_ptr<pvd::PushProvider> &provider)
			: PushStream(StreamSourceType::Rtmp, channel_id, provider)
		{
		}

		PushStreamType GetPushStreamType() override
		{
			return PushStreamType::UNKNOWN;
		}

		bool OnDataReceived(const std::shared_ptr<const ov::Data> &data) override
		{
			// Which call this is, so an immutable handler can tell them apart. Every field below is
			// atomic because a test may run two of these at once, and `_handler` is never reassigned
			// while one is running.
			const int index		= _call_count.fetch_add(1);

			_handler_ran		= true;
			_marked_in_handler	= IsProcessingData();
			_elapsed_in_handler = GetElapsedMsSinceLastReceived();

			if (_handler != nullptr)
			{
				_handler(index);
			}

			return _handler_result.load();
		}

		// The handler's own behavior, set before any call and not changed afterwards
		std::function<void(int index)> _handler = nullptr;
		std::atomic<bool> _handler_result		= true;

		// What the handlers saw while they were running
		std::atomic<int> _call_count			= 0;
		std::atomic<bool> _handler_ran			= false;
		std::atomic<bool> _marked_in_handler	= false;
		std::atomic<time_t> _elapsed_in_handler = 0;
	};

	// Holds the provider and one channel already registered with it, which is the state every
	// concrete push provider hands to `OnDataReceived()`.
	struct Fixture
	{
		static constexpr uint32_t CHANNEL_ID = 7;

		Fixture()
		{
			router	 = std::make_shared<StubMediaRouter>();
			provider = std::make_shared<StubPushProvider>(server_config, router);
			channel	 = std::make_shared<StubPushStream>(CHANNEL_ID, provider);

			provider->OnChannelCreated(CHANNEL_ID, channel);
		}

		std::shared_ptr<const ov::Data> MakeData() const
		{
			return std::make_shared<ov::Data>("data", 4);
		}

		// Registers an application whose RTMP provider carries `rtmp_body`, and returns its name.
		// Without this the channel has no application, which is a different code path.
		info::VHostAppName CreateApplication(const std::string &rtmp_body)
		{
			char path[]	 = "/tmp/ome_stream_lifecycle_test_XXXXXX";
			const int fd = ::mkstemp(path);
			if (fd < 0)
			{
				return info::VHostAppName::InvalidVHostAppName();
			}
			::close(fd);

			{
				std::ofstream out(path);
				out << "<VirtualHost>"
					<< "<Name>default</Name>"
					<< "<Host><Names><Name>*</Name></Names></Host>"
					<< "<Applications><Application>"
					<< "<Name>app</Name><Type>live</Type>"
					<< "<Providers><RTMP>" << rtmp_body << "</RTMP></Providers>"
					<< "</Application></Applications>"
					<< "</VirtualHost>";
			}

			cfg::vhost::VirtualHost vhost_config;

			try
			{
				cfg::DataSource data_source(cfg::DataType::Xml, path, cfg::ItemName("VirtualHost"));
				vhost_config.FromDataSource("VirtualHost", cfg::ItemName("VirtualHost"), data_source);
			}
			catch (const cfg::ConfigError &)
			{
				::remove(path);
				return info::VHostAppName::InvalidVHostAppName();
			}

			::remove(path);

			const auto app_list = vhost_config.GetApplicationList();
			if (app_list.empty())
			{
				return info::VHostAppName::InvalidVHostAppName();
			}

			host_info			= std::make_shared<info::Host>("test-server", "test-server-id", vhost_config);
			auto vhost_app_name = info::VHostAppName("default", "app");
			application_info	= std::make_shared<TestApplicationInfo>(*host_info, vhost_app_name, app_list[0]);

			if (provider->OnCreateApplication(*application_info) == false)
			{
				return info::VHostAppName::InvalidVHostAppName();
			}

			return vhost_app_name;
		}

		cfg::Server server_config;
		std::shared_ptr<StubMediaRouter> router;
		std::shared_ptr<StubPushProvider> provider;
		std::shared_ptr<StubPushStream> channel;
		std::shared_ptr<info::Host> host_info;
		std::shared_ptr<TestApplicationInfo> application_info;
	};
}  // namespace

TEST(PushStreamLifecycle, ChannelStartsWithTheCreationDefaultBudget)
{
	Fixture f;

	EXPECT_EQ(f.channel->GetPacketSilenceTimeoutMs(), pvd::DEFAULT_PUSH_CHANNEL_PACKET_SILENCE_TIMEOUT_MS);
}

TEST(PushStreamLifecycle, ChannelWithoutDataIsNeverJudged)
{
	Fixture f;

	// The runner compares this against the budget, so a negative value is what keeps a channel that
	// has not received anything yet from being deleted on the first tick.
	EXPECT_LT(f.channel->GetElapsedMsSinceLastReceived(), 0);
}

TEST(PushStreamLifecycle, DataForAnUnknownChannelIsRejected)
{
	Fixture f;

	EXPECT_FALSE(f.provider->OnDataReceived(Fixture::CHANNEL_ID + 1, f.MakeData()));
	EXPECT_FALSE(f.channel->_handler_ran);
}

TEST(PushStreamLifecycle, ProcessingMarkIsSetOnlyWhileTheHandlerRuns)
{
	Fixture f;

	EXPECT_FALSE(f.channel->IsProcessingData());

	EXPECT_TRUE(f.provider->OnDataReceived(Fixture::CHANNEL_ID, f.MakeData()));

	EXPECT_TRUE(f.channel->_handler_ran);
	EXPECT_TRUE(f.channel->_marked_in_handler);
	EXPECT_FALSE(f.channel->IsProcessingData());
}

TEST(PushStreamLifecycle, ProcessingMarkIsClearedWhenTheHandlerThrows)
{
	Fixture f;

	f.channel->_handler = [](int index) { throw std::runtime_error("handler failed"); };

	EXPECT_THROW(f.provider->OnDataReceived(Fixture::CHANNEL_ID, f.MakeData()), std::runtime_error);

	// A mark left behind would make this channel unreapable for the rest of its life.
	EXPECT_FALSE(f.channel->IsProcessingData());
}

TEST(PushStreamLifecycle, AcceptedDataUpdatesTheReceivedTime)
{
	Fixture f;

	EXPECT_TRUE(f.provider->OnDataReceived(Fixture::CHANNEL_ID, f.MakeData()));

	// The handler runs before the time is updated, so it still sees an unjudgeable channel.
	EXPECT_LT(f.channel->_elapsed_in_handler, 0);
	EXPECT_GE(f.channel->GetElapsedMsSinceLastReceived(), 0);
}

TEST(PushStreamLifecycle, RejectedDataLeavesTheReceivedTimeAlone)
{
	Fixture f;

	f.channel->_handler_result = false;

	EXPECT_TRUE(f.provider->OnDataReceived(Fixture::CHANNEL_ID, f.MakeData()));

	EXPECT_TRUE(f.channel->_handler_ran);
	EXPECT_LT(f.channel->GetElapsedMsSinceLastReceived(), 0);
}

TEST(PushStreamLifecycle, HandlerLongerThanTheBudgetDoesNotLeaveTheChannelReapable)
{
	Fixture f;

	constexpr time_t BUDGET_MS = 100;
	constexpr int HANDLER_MS   = 300;

	f.channel->SetPacketSilenceTimeoutMs(BUDGET_MS);
	f.channel->_handler = [HANDLER_MS](int index) { std::this_thread::sleep_for(std::chrono::milliseconds(HANDLER_MS)); };

	EXPECT_TRUE(f.provider->OnDataReceived(Fixture::CHANNEL_ID, f.MakeData()));

	// Access control can hold a handler for seconds, and the runner must not read that as client
	// silence, so once the mark is gone the elapsed time has to be the time since the handler
	// finished rather than since it started.
	EXPECT_FALSE(f.channel->IsProcessingData());
	EXPECT_LT(f.channel->GetElapsedMsSinceLastReceived(), BUDGET_MS);
}

TEST(PushStreamLifecycle, ProcessingMarkSurvivesUntilTheLastOverlappingHandlerLeaves)
{
	Fixture f;

	std::promise<void> first_entered;
	std::promise<void> second_left;
	auto second_left_future = second_left.get_future().share();

	// One handler for both calls, branching on the call index, because reassigning the handler while
	// the first call is inside it would be a data race on the same object. The first call stays inside
	// until the second one has come and gone, which is the order a flag cannot express: the second
	// handler leaving must not clear the mark the first one still needs.
	f.channel->_handler		= [&first_entered, second_left_future](int index) {
		if (index == 0)
		{
			first_entered.set_value();
			second_left_future.wait();
		}
	};

	auto first = std::async(std::launch::async, [&f]() {
		return f.provider->OnDataReceived(Fixture::CHANNEL_ID, f.MakeData());
	});

	first_entered.get_future().wait();

	// Reuse the same channel from this thread, as a base class with several sockets or workers per
	// channel would. This call returns immediately.
	EXPECT_TRUE(f.provider->OnDataReceived(Fixture::CHANNEL_ID, f.MakeData()));

	const bool marked_after_second_left = f.channel->IsProcessingData();

	second_left.set_value();

	// `get()` rather than `wait()`, so an exception from the other thread fails the test.
	EXPECT_TRUE(first.get());

	EXPECT_TRUE(marked_after_second_left);
	EXPECT_FALSE(f.channel->IsProcessingData());
}

TEST(PushStreamLifecycle, EndingTheFirstMediaWaitKeepsTheBudgetWhenTheApplicationIsUnknown)
{
	Fixture f;

	constexpr time_t WAIT_MS = 30000;

	f.channel->SetPacketSilenceTimeoutMs(WAIT_MS);

	// The stub provider has no applications, which is also what a channel sees when its application
	// is deleted while it waits. Losing the budget here would leave the channel with no guard at all.
	f.channel->EndFirstMediaWait(info::VHostAppName::InvalidVHostAppName());

	EXPECT_EQ(f.channel->GetPacketSilenceTimeoutMs(), WAIT_MS);
}

TEST(PushStreamLifecycle, FirstMediaEndsTheWaitOnTheUnpublishedFallbackWhenNothingIsConfigured)
{
	Fixture f;

	const auto vhost_app_name = f.CreateApplication("");
	ASSERT_TRUE(vhost_app_name.IsValid());

	f.channel->ApplyConfiguredFirstMediaWaitTimeoutMs(vhost_app_name);
	EXPECT_EQ(f.channel->GetPacketSilenceTimeoutMs(), cfg::vhost::app::pvd::DEFAULT_FIRST_MEDIA_WAIT_TIMEOUT_MS);

	f.channel->EndFirstMediaWait(vhost_app_name);

	// The channel is still unpublished, so it falls back to the budget it was created with rather than
	// keeping a budget sized for a source that had not sent anything yet.
	EXPECT_EQ(f.channel->GetPacketSilenceTimeoutMs(), pvd::DEFAULT_PUSH_CHANNEL_PACKET_SILENCE_TIMEOUT_MS);
}

TEST(PushStreamLifecycle, FirstMediaEndsTheWaitOnTheUnpublishedFallbackWhenTheTimeoutIsExplicitlyZero)
{
	Fixture f;

	const auto vhost_app_name = f.CreateApplication("<PacketSilenceTimeoutMs>0</PacketSilenceTimeoutMs>");
	ASSERT_TRUE(vhost_app_name.IsValid());

	f.channel->ApplyConfiguredFirstMediaWaitTimeoutMs(vhost_app_name);
	f.channel->EndFirstMediaWait(vhost_app_name);

	// An explicit `0` disables the timeout for a published stream. Honoring it here would leave an
	// unpublished channel with no guard at all, so it holds its connection for as long as it likes.
	EXPECT_EQ(f.channel->GetPacketSilenceTimeoutMs(), pvd::DEFAULT_PUSH_CHANNEL_PACKET_SILENCE_TIMEOUT_MS);
}

TEST(PushStreamLifecycle, FirstMediaEndsTheWaitOnAnOperatorConfiguredTimeout)
{
	Fixture f;

	const auto vhost_app_name = f.CreateApplication("<PacketSilenceTimeoutMs>4000</PacketSilenceTimeoutMs>");
	ASSERT_TRUE(vhost_app_name.IsValid());

	f.channel->ApplyConfiguredFirstMediaWaitTimeoutMs(vhost_app_name);
	f.channel->EndFirstMediaWait(vhost_app_name);

	EXPECT_EQ(f.channel->GetPacketSilenceTimeoutMs(), 4000);
}

TEST(PushStreamLifecycle, MediaThatArrivesBeforeTheWaitIsSizedDoesNotConsumeIt)
{
	Fixture f;

	const auto vhost_app_name = f.CreateApplication(
		"<FirstMediaWaitTimeoutMs>8000</FirstMediaWaitTimeoutMs>"
		"<PacketSilenceTimeoutMs>4000</PacketSilenceTimeoutMs>");
	ASSERT_TRUE(vhost_app_name.IsValid());

	// A message dispatcher hands media to a channel whether or not it has asked to publish yet, so
	// this can run before the provider sized the wait.
	f.channel->EndFirstMediaWait(vhost_app_name);
	EXPECT_EQ(f.channel->GetPacketSilenceTimeoutMs(), pvd::DEFAULT_PUSH_CHANNEL_PACKET_SILENCE_TIMEOUT_MS);

	f.channel->ApplyConfiguredFirstMediaWaitTimeoutMs(vhost_app_name);
	EXPECT_EQ(f.channel->GetPacketSilenceTimeoutMs(), 8000);

	// The wait has to still be endable here. Had the earlier call consumed it, the channel would stay
	// on the 8000 ms budget until it publishes, and a source slower than that would never get there.
	f.channel->EndFirstMediaWait(vhost_app_name);
	EXPECT_EQ(f.channel->GetPacketSilenceTimeoutMs(), 4000);

	// Only the first call after the wait was sized has an effect.
	f.channel->SetPacketSilenceTimeoutMs(1234);
	f.channel->EndFirstMediaWait(vhost_app_name);
	EXPECT_EQ(f.channel->GetPacketSilenceTimeoutMs(), 1234);
}
