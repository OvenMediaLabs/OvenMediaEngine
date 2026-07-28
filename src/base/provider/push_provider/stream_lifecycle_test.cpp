//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2026 OvenMediaLabs. All rights reserved.
//
//==============================================================================
#include <base/mediarouter/mediarouter_interface.h>
#include <gtest/gtest.h>

#include <chrono>
#include <functional>
#include <thread>

#include "provider.h"
#include "stream.h"

//  Covers the channel state the channel task runner reads before it deletes a channel:
//  the silence budget, the last received time, and the mark that says a handler is running.
//
//  These are what decide whether a live input survives, and the runner reads them from another
//  thread, so a wrong transition shows up as a deleted stream rather than as a failure here.
namespace
{
	class StubPushProvider : public pvd::PushProvider
	{
	public:
		explicit StubPushProvider(const cfg::Server &server_config)
			: PushProvider(server_config, nullptr)
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

		std::shared_ptr<pvd::Application> OnCreateProviderApplication(const info::Application &app_info) override
		{
			return nullptr;
		}

		bool OnDeleteProviderApplication(const std::shared_ptr<pvd::Application> &application) override
		{
			return true;
		}

		// Concrete providers reach these from their own socket callbacks, where they are protected.
		using PushProvider::OnChannelCreated;
		using PushProvider::OnDataReceived;
	};

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
			_handler_ran		 = true;
			_marked_in_handler	 = IsProcessingData();
			_elapsed_in_handler	 = GetElapsedMsSinceLastReceived();

			if (_handler != nullptr)
			{
				_handler();
			}

			return _handler_result;
		}

		// The handler's own behavior, set per test
		std::function<void()> _handler = nullptr;
		bool _handler_result		   = true;

		// What the handler saw while it was running
		bool _handler_ran		   = false;
		bool _marked_in_handler	   = false;
		time_t _elapsed_in_handler = 0;
	};

	// Holds the provider and one channel already registered with it, which is the state every
	// concrete push provider hands to `OnDataReceived()`.
	struct Fixture
	{
		static constexpr uint32_t CHANNEL_ID = 7;

		Fixture()
		{
			provider = std::make_shared<StubPushProvider>(server_config);
			channel	 = std::make_shared<StubPushStream>(CHANNEL_ID, provider);

			provider->OnChannelCreated(CHANNEL_ID, channel);
		}

		std::shared_ptr<const ov::Data> MakeData() const
		{
			return std::make_shared<ov::Data>("data", 4);
		}

		cfg::Server server_config;
		std::shared_ptr<StubPushProvider> provider;
		std::shared_ptr<StubPushStream> channel;
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

	f.channel->_handler = []() { throw std::runtime_error("handler failed"); };

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

	constexpr time_t BUDGET_MS	= 100;
	constexpr int HANDLER_MS	= 300;

	f.channel->SetPacketSilenceTimeoutMs(BUDGET_MS);
	f.channel->_handler = [HANDLER_MS]() { std::this_thread::sleep_for(std::chrono::milliseconds(HANDLER_MS)); };

	EXPECT_TRUE(f.provider->OnDataReceived(Fixture::CHANNEL_ID, f.MakeData()));

	// Access control can hold a handler for seconds, and the runner must not read that as client
	// silence, so once the mark is gone the elapsed time has to be the time since the handler
	// finished rather than since it started.
	EXPECT_FALSE(f.channel->IsProcessingData());
	EXPECT_LT(f.channel->GetElapsedMsSinceLastReceived(), BUDGET_MS);
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
