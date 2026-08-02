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
#include <stdlib.h>
#include <unistd.h>

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

//  Covers the channel state `PushProvider::ChannelTaskRunner()` reads before it deletes a channel:
//  `PacketSilenceTimeoutMs`, the last received time, and the mark `BeginProcessingData()` sets.
//
//  These are what decide whether a live input survives,
//  and `ChannelTaskRunner()` reads them from another thread,
//  so a wrong transition shows up as a deleted stream rather than as a failure here.
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
		// `ChannelTaskRunner()` and the socket callbacks call this one.
		using PushProvider::OnChannelDeleted;
	};

	// The provider registers its applications with the router before storing them,
	// so a channel cannot reach its application without one.
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

	// Both constructors are reachable only from a subclass,
	// because normally the orchestrator and the concrete providers are the ones that build these.
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

		// The real one tears the stream down through the media router,
		// which these stubs cannot stand in for.
		// Only the fact that this path was taken matters here.
		bool DeleteStream(const std::shared_ptr<pvd::Stream> &stream) override
		{
			_deleted_stream_count++;
			return true;
		}

		std::atomic<int> _deleted_stream_count = 0;
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

		void CloseTransport() override
		{
			_close_transport_count++;
		}

		std::atomic<int> _close_transport_count = 0;

		bool OnDataReceived(const std::shared_ptr<const ov::Data> &data) override
		{
			// Which call this is, so an immutable handler can tell them apart.
			// Every field below is atomic because a test may run two of these at once,
			// and `_handler` is never reassigned while one is running.
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

	// Holds the provider and one channel already registered with it,
	// which is the state every concrete push provider hands to `OnDataReceived()`.
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

TEST(PushStreamLifecycle, ChannelStartsWithTheCreationDefaultTimeout)
{
	Fixture f;

	EXPECT_EQ(f.channel->GetPacketSilenceTimeoutMs(), pvd::DEFAULT_PUSH_CHANNEL_PACKET_SILENCE_TIMEOUT_MS);
}

TEST(PushStreamLifecycle, ChannelWithoutDataIsNeverSilentBeyondItsTimeout)
{
	Fixture f;

	// The runner compares this against the budget,
	// so a negative value is what keeps a channel that has never received anything from being deleted.
	EXPECT_LT(f.channel->GetElapsedMsSinceLastReceived(), 0);
}

TEST(PushStreamLifecycle, DataForAnUnknownChannelIsRejected)
{
	Fixture f;

	EXPECT_FALSE(f.provider->OnDataReceived(Fixture::CHANNEL_ID + 1, f.MakeData()));
	EXPECT_FALSE(f.channel->_handler_ran);
}

TEST(PushStreamLifecycle, TheMarkIsSetOnlyWhileOnDataReceivedRuns)
{
	Fixture f;

	EXPECT_FALSE(f.channel->IsProcessingData());

	EXPECT_TRUE(f.provider->OnDataReceived(Fixture::CHANNEL_ID, f.MakeData()));

	EXPECT_TRUE(f.channel->_handler_ran);
	EXPECT_TRUE(f.channel->_marked_in_handler);
	EXPECT_FALSE(f.channel->IsProcessingData());
}

TEST(PushStreamLifecycle, TheMarkIsClearedWhenOnDataReceivedThrows)
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

TEST(PushStreamLifecycle, OnDataReceivedLongerThanTheTimeoutDoesNotLeaveTheChannelReservable)
{
	Fixture f;

	constexpr time_t BUDGET_MS = 100;
	constexpr int HANDLER_MS   = 300;

	f.channel->SetPacketSilenceTimeoutMs(BUDGET_MS);
	f.channel->_handler = [HANDLER_MS](int index) { std::this_thread::sleep_for(std::chrono::milliseconds(HANDLER_MS)); };

	EXPECT_TRUE(f.provider->OnDataReceived(Fixture::CHANNEL_ID, f.MakeData()));

	// Access control can hold a handler for seconds, and the runner must not read that as client silence.
	// So once the mark is gone, the elapsed time has to run from when the handler finished,
	// not from when it started.
	EXPECT_FALSE(f.channel->IsProcessingData());
	EXPECT_LT(f.channel->GetElapsedMsSinceLastReceived(), BUDGET_MS);
}

TEST(PushStreamLifecycle, TheMarkSurvivesUntilTheLastOverlappingOnDataReceivedLeaves)
{
	Fixture f;

	std::promise<void> first_entered;
	std::promise<void> second_left;
	auto second_left_future = second_left.get_future().share();

	// One handler for both calls, branching on the call index:
	// reassigning it while the first call is inside would be a data race on the same object.
	// The first call stays inside until the second one has come and gone,
	// which is the order a flag cannot express.
	// The second handler leaving must not clear the mark the first one still needs.
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

	// Reuse the same channel from this thread,
	// as a base class with several sockets or workers per channel would. This call returns immediately.
	EXPECT_TRUE(f.provider->OnDataReceived(Fixture::CHANNEL_ID, f.MakeData()));

	const bool marked_after_second_left = f.channel->IsProcessingData();

	second_left.set_value();

	// `get()` rather than `wait()`, so an exception from the other thread fails the test.
	EXPECT_TRUE(first.get());

	EXPECT_TRUE(marked_after_second_left);
	EXPECT_FALSE(f.channel->IsProcessingData());
}

TEST(PushStreamLifecycle, EndingTheFirstMediaWaitKeepsTheTimeoutWhenTheApplicationIsUnknown)
{
	Fixture f;

	const auto vhost_app_name = f.CreateApplication("<FirstMediaWaitTimeoutMs>8000</FirstMediaWaitTimeoutMs>");
	ASSERT_TRUE(vhost_app_name.IsValid());

	// The wait has to be running for this to reach the lookup at all.
	f.channel->ApplyConfiguredFirstMediaWaitTimeoutMs(vhost_app_name);
	ASSERT_EQ(f.channel->GetPacketSilenceTimeoutMs(), 8000);

	// A name that resolves to nothing, which is also what a channel sees
	// when its application is deleted while it waits.
	// Losing the budget here would leave the channel with no guard at all.
	f.channel->EndFirstMediaWait(info::VHostAppName::InvalidVHostAppName());

	EXPECT_EQ(f.channel->GetPacketSilenceTimeoutMs(), 8000);

	// The failed lookup must not have spent the wait either, so the next packet still ends it.
	f.channel->EndFirstMediaWait(vhost_app_name);

	EXPECT_EQ(f.channel->GetPacketSilenceTimeoutMs(), pvd::DEFAULT_PUSH_CHANNEL_PACKET_SILENCE_TIMEOUT_MS);
}

TEST(PushStreamLifecycle, AnAbsentFirstMediaWaitOptionChangesNothing)
{
	Fixture f;

	const auto vhost_app_name = f.CreateApplication("");
	ASSERT_TRUE(vhost_app_name.IsValid());

	// With the option off, this leaves the channel on the timeout it was created with, and never starts
	// the wait, so the first media packet has nothing to end.
	f.channel->ApplyConfiguredFirstMediaWaitTimeoutMs(vhost_app_name);
	EXPECT_EQ(f.channel->GetPacketSilenceTimeoutMs(), pvd::DEFAULT_PUSH_CHANNEL_PACKET_SILENCE_TIMEOUT_MS);

	f.channel->EndFirstMediaWait(vhost_app_name);

	EXPECT_EQ(f.channel->GetPacketSilenceTimeoutMs(), pvd::DEFAULT_PUSH_CHANNEL_PACKET_SILENCE_TIMEOUT_MS);
}

TEST(PushStreamLifecycle, AnAbsentFirstMediaWaitOptionStillHonorsPacketSilenceTimeoutMs)
{
	Fixture f;

	const auto vhost_app_name = f.CreateApplication("<PacketSilenceTimeoutMs>4000</PacketSilenceTimeoutMs>");
	ASSERT_TRUE(vhost_app_name.IsValid());

	// With the option unset, this applies `PacketSilenceTimeoutMs` and starts no wait.
	f.channel->ApplyConfiguredFirstMediaWaitTimeoutMs(vhost_app_name);

	EXPECT_EQ(f.channel->GetPacketSilenceTimeoutMs(), 4000);
}

TEST(PushStreamLifecycle, FirstMediaEndsTheWaitOnTheUnpublishedFallbackWhenTheTimeoutIsExplicitlyZero)
{
	Fixture f;

	const auto vhost_app_name = f.CreateApplication("<PacketSilenceTimeoutMs>0</PacketSilenceTimeoutMs>");
	ASSERT_TRUE(vhost_app_name.IsValid());

	f.channel->ApplyConfiguredFirstMediaWaitTimeoutMs(vhost_app_name);
	f.channel->EndFirstMediaWait(vhost_app_name);

	// An explicit `0` disables the timeout for a published stream.
	// Honoring it here would leave an unpublished channel with no guard at all,
	// so it holds its connection for as long as it likes.
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

	// A message dispatcher hands media to a channel whether or not it has asked to publish yet,
	// so this can run before the provider sized the wait.
	f.channel->EndFirstMediaWait(vhost_app_name);
	EXPECT_EQ(f.channel->GetPacketSilenceTimeoutMs(), pvd::DEFAULT_PUSH_CHANNEL_PACKET_SILENCE_TIMEOUT_MS);

	f.channel->ApplyConfiguredFirstMediaWaitTimeoutMs(vhost_app_name);
	EXPECT_EQ(f.channel->GetPacketSilenceTimeoutMs(), 8000);

	// The wait has to still be endable here.
	// Had the earlier call consumed it, the channel would stay on the 8000 ms budget until it publishes,
	// and a source slower than that would never get there.
	f.channel->EndFirstMediaWait(vhost_app_name);
	EXPECT_EQ(f.channel->GetPacketSilenceTimeoutMs(), 4000);

	// Only the first call after the wait was sized has an effect.
	f.channel->SetPacketSilenceTimeoutMs(1234);
	f.channel->EndFirstMediaWait(vhost_app_name);
	EXPECT_EQ(f.channel->GetPacketSilenceTimeoutMs(), 1234);
}

//  `ChannelTaskRunner()` reads a channel's state on its own thread,
//  while `PushStream::OnDataReceived()` can be writing it.
//  These pin the rule it decides by: one read of the state,
//  and no deletion if that read has been replaced since.
namespace
{
	// Leaves the channel silent for longer than its `PacketSilenceTimeoutMs` allows,
	// which is the only state `ChannelTaskRunner()` ever deletes a channel from.
	void MakeSilentBeyondTimeout(const std::shared_ptr<StubPushStream> &channel)
	{
		channel->SetPacketSilenceTimeoutMs(1);
		channel->UpdateLastReceivedTime();

		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}
}  // namespace

TEST(PushStreamLifecycle, ASilentChannelIsReadInOnePassBeforeItIsReserved)
{
	Fixture f;

	MakeSilentBeyondTimeout(f.channel);

	const auto state = f.channel->GetSilenceState();

	EXPECT_TRUE(state.IsSilentBeyondTimeout());
	EXPECT_TRUE(f.channel->TryBeginReaping(state));
}

TEST(PushStreamLifecycle, AChannelWithNoTimeoutIsNeverSilentBeyondIt)
{
	Fixture f;

	f.channel->UpdateLastReceivedTime();
	f.channel->SetPacketSilenceTimeoutMs(0);

	EXPECT_FALSE(f.channel->GetSilenceState().IsSilentBeyondTimeout());
}

TEST(PushStreamLifecycle, TryBeginReapingFailsWhenTheTimeoutChangesUnderIt)
{
	Fixture f;

	MakeSilentBeyondTimeout(f.channel);

	const auto state = f.channel->GetSilenceState();
	ASSERT_TRUE(state.IsSilentBeyondTimeout());

	// What `PushApplication::JoinStream()` does the moment a stream publishes.
	// Acting on the state above would delete a stream that has just gone live.
	f.channel->SetPacketSilenceTimeoutMs(0);

	EXPECT_FALSE(f.channel->TryBeginReaping(state));
}

TEST(PushStreamLifecycle, TryBeginReapingFailsWhenDataArrivesUnderIt)
{
	Fixture f;

	MakeSilentBeyondTimeout(f.channel);

	const auto state = f.channel->GetSilenceState();
	ASSERT_TRUE(state.IsSilentBeyondTimeout());

	f.channel->UpdateLastReceivedTime();

	EXPECT_FALSE(f.channel->TryBeginReaping(state));
}

TEST(PushStreamLifecycle, TryBeginReapingFailsWhenOnDataReceivedStartsUnderIt)
{
	Fixture f;

	MakeSilentBeyondTimeout(f.channel);

	const auto state = f.channel->GetSilenceState();
	ASSERT_TRUE(state.IsSilentBeyondTimeout());

	EXPECT_TRUE(f.channel->BeginProcessingData());

	EXPECT_FALSE(f.channel->TryBeginReaping(state));

	f.channel->EndProcessingData();
}

TEST(PushStreamLifecycle, TryBeginReapingFailsWhenTheFirstMediaPacketEndsTheWaitUnderIt)
{
	Fixture f;

	// The option has to be set for there to be a wait to end at all.
	const auto vhost_app_name = f.CreateApplication("<FirstMediaWaitTimeoutMs>30000</FirstMediaWaitTimeoutMs>");
	ASSERT_TRUE(vhost_app_name.IsValid());

	f.channel->ApplyConfiguredFirstMediaWaitTimeoutMs(vhost_app_name);
	MakeSilentBeyondTimeout(f.channel);

	const auto state = f.channel->GetSilenceState();
	ASSERT_TRUE(state.IsSilentBeyondTimeout());

	// `OnDataReceived()` ends the wait and lowers the timeout while `ChannelTaskRunner()` sits between
	// reading this state and acting on it. Acting on it here would delete a live channel.
	f.channel->EndFirstMediaWait(vhost_app_name);

	EXPECT_FALSE(f.channel->TryBeginReaping(state));
}

TEST(PushStreamLifecycle, AReservedChannelRefusesEveryLaterOnDataReceived)
{
	Fixture f;

	MakeSilentBeyondTimeout(f.channel);

	const auto state = f.channel->GetSilenceState();
	ASSERT_TRUE(f.channel->TryBeginReaping(state));

	// A receive thread that arrives after `TryBeginReaping()` succeeded.
	// Letting it in would run `OnDataReceived()` against a channel already being torn down.
	EXPECT_FALSE(f.channel->BeginProcessingData());
	EXPECT_FALSE(f.channel->IsProcessingData());

	// The data has nowhere to go, so the provider drops it instead of handing it to the channel.
	EXPECT_FALSE(f.provider->OnDataReceived(Fixture::CHANNEL_ID, f.MakeData()));
	EXPECT_FALSE(f.channel->_handler_ran);
}

TEST(PushStreamLifecycle, OnlyOneTryBeginReapingSucceedsOnAChannel)
{
	Fixture f;

	MakeSilentBeyondTimeout(f.channel);

	const auto state = f.channel->GetSilenceState();

	EXPECT_TRUE(f.channel->TryBeginReaping(state));
	EXPECT_FALSE(f.channel->TryBeginReaping(state));
}

TEST(PushStreamLifecycle, ExactlyOneOfTryBeginReapingAndBeginProcessingDataWins)
{
	// Both threads meet on the same channel at the same moment, which is the race between
	// `ChannelTaskRunner()` committing a deletion and a receive thread entering `OnDataReceived()`.
	for (int round = 0; round < 200; round++)
	{
		Fixture f;

		MakeSilentBeyondTimeout(f.channel);

		const auto state = f.channel->GetSilenceState();
		ASSERT_TRUE(state.IsSilentBeyondTimeout());

		std::atomic<int> arrived   = 0;
		std::atomic<bool> reserved = false;
		std::atomic<bool> entered  = false;

		auto barrier			   = [&arrived]() {
			arrived.fetch_add(1);

			while (arrived.load() < 2)
			{
				std::this_thread::yield();
			}
		};

		auto reaper = std::async(std::launch::async, [&]() {
			barrier();
			reserved = f.channel->TryBeginReaping(state);
		});

		barrier();
		entered = f.channel->BeginProcessingData();

		reaper.wait();

		// Never both: a call inside blocks `TryBeginReaping()`, and a reservation refuses the call.
		EXPECT_NE(reserved.load(), entered.load());

		if (entered.load())
		{
			f.channel->EndProcessingData();
		}
	}
}

TEST(PushStreamLifecycle, ALosingTryBeginReapingCostsNoData)
{
	Fixture f;

	MakeSilentBeyondTimeout(f.channel);

	const auto state = f.channel->GetSilenceState();
	ASSERT_TRUE(state.IsSilentBeyondTimeout());

	// A packet arrives between `GetSilenceState()` reading the state and `TryBeginReaping()` using it,
	// which is what makes that state stale.
	ASSERT_TRUE(f.provider->OnDataReceived(Fixture::CHANNEL_ID, f.MakeData()));

	EXPECT_FALSE(f.channel->TryBeginReaping(state));

	// `TryBeginReaping()` lost, so it must not have held the channel while it found that out.
	// A call that arrives now still gets in, and its bytes still reach the channel.
	f.channel->_call_count	= 0;
	f.channel->_handler_ran = false;

	EXPECT_TRUE(f.provider->OnDataReceived(Fixture::CHANNEL_ID, f.MakeData()));
	EXPECT_TRUE(f.channel->_handler_ran);
	EXPECT_EQ(f.channel->_call_count.load(), 1);
}

TEST(PushStreamLifecycle, AStaleTryBeginReapingRacingAReceiveNeverDropsData)
{
	// The state is stale before `TryBeginReaping()` starts, so it has to lose every round.
	// Whether it loses before or after the receive thread arrives, that thread must always get in.
	for (int round = 0; round < 200; round++)
	{
		Fixture f;

		MakeSilentBeyondTimeout(f.channel);

		const auto state = f.channel->GetSilenceState();
		ASSERT_TRUE(state.IsSilentBeyondTimeout());

		// Moves the change count on, which is what leaves the state above behind.
		f.channel->SetPacketSilenceTimeoutMs(1);

		std::atomic<int> arrived   = 0;
		std::atomic<bool> reserved = false;

		auto barrier			   = [&arrived]() {
			arrived.fetch_add(1);

			while (arrived.load() < 2)
			{
				std::this_thread::yield();
			}
		};

		auto reaper = std::async(std::launch::async, [&]() {
			barrier();
			reserved = f.channel->TryBeginReaping(state);
		});

		barrier();
		const bool handled = f.provider->OnDataReceived(Fixture::CHANNEL_ID, f.MakeData());

		reaper.wait();

		EXPECT_FALSE(reserved.load());
		EXPECT_TRUE(handled);
		EXPECT_TRUE(f.channel->_handler_ran);
	}
}

TEST(PushStreamLifecycle, CountingStateChangesLeavesTheOnDataReceivedCountAlone)
{
	Fixture f;

	// Handlers, the change counter and the reaping flag share one word,
	// so counting a change must not reach the other two.
	// A handler stays inside across all of these.
	ASSERT_TRUE(f.channel->BeginProcessingData());

	for (int i = 0; i < 1000; i++)
	{
		f.channel->UpdateLastReceivedTime();
		f.channel->SetPacketSilenceTimeoutMs(1);

		ASSERT_TRUE(f.channel->IsProcessingData());
		ASSERT_FALSE(f.channel->GetSilenceState().IsSilentBeyondTimeout());
	}

	f.channel->EndProcessingData();

	EXPECT_FALSE(f.channel->IsProcessingData());

	// Two changes per round have moved the counter, and `TryBeginReaping()` still succeeds now.
	std::this_thread::sleep_for(std::chrono::milliseconds(10));

	const auto state = f.channel->GetSilenceState();

	ASSERT_TRUE(state.IsSilentBeyondTimeout());
	EXPECT_TRUE(f.channel->TryBeginReaping(state));
}

TEST(PushStreamLifecycle, DeletingAChannelThatNeverJoinedAnApplicationClosesItsTransport)
{
	Fixture f;

	ASSERT_FALSE(f.channel->DoesBelongApplication());

	EXPECT_TRUE(f.provider->OnChannelDeleted(f.channel));

	// Nothing else tears the transport down on this path:
	// `Application::DeleteStream()` never runs, so neither does `Stream::Stop()`,
	// and the physical port owns the socket rather than the stream.
	// Without this the client keeps a connection it believes is still streaming.
	EXPECT_EQ(f.channel->_close_transport_count.load(), 1);
}

TEST(PushStreamLifecycle, DeletingAJoinedChannelLeavesItsTransportToTheApplication)
{
	Fixture f;

	const auto vhost_app_name = f.CreateApplication("");
	ASSERT_TRUE(vhost_app_name.IsValid());

	auto application = f.provider->GetApplicationByName(vhost_app_name);
	ASSERT_NE(application, nullptr);

	f.channel->SetApplication(application);
	ASSERT_TRUE(f.channel->DoesBelongApplication());

	EXPECT_TRUE(f.provider->OnChannelDeleted(f.channel));

	// The application's own teardown reaches `Stream::Stop()`, which closes the transport with the rest
	// of it. Closing it here as well would be a double teardown.
	EXPECT_EQ(f.channel->_close_transport_count.load(), 0);

	auto stub = std::dynamic_pointer_cast<StubPushApplication>(application);
	ASSERT_NE(stub, nullptr);
	EXPECT_EQ(stub->_deleted_stream_count.load(), 1);
}
