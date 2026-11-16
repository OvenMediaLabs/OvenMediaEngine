//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2026 OvenMediaLabs. All rights reserved.
//
//==============================================================================
#include <base/mediarouter/mediarouter_interface.h>
#include <base/provider/pull_provider/application.h>
#include <base/provider/pull_provider/provider.h>
#include <base/provider/pull_provider/stream.h>
#include <base/provider/pull_provider/stream_props.h>
#include <config/config.h>
#include <gtest/gtest.h>
#include <stdlib.h>
#include <unistd.h>

#include <cstdio>
#include <fstream>
#include <functional>
#include <memory>
#include <string>

//  Covers `PullApplication::DeleteStream()` running while `PullApplication::DeleteAllStreams()`
//  deletes the application.
namespace
{
	class StubPullProvider : public pvd::PullProvider
	{
	public:
		StubPullProvider(const cfg::Server &server_config, const std::shared_ptr<MediaRouterInterface> &router)
			: PullProvider(server_config, router)
		{
		}

		ProviderType GetProviderType() const override
		{
			return ProviderType::Ovt;
		}

		ProviderStreamDirection GetProviderStreamDirection() const override
		{
			return ProviderStreamDirection::Pull;
		}

		const char *GetProviderName() const override
		{
			return "StubPullProvider";
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
	};

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

	class TestApplicationInfo : public info::Application
	{
	public:
		TestApplicationInfo(const info::Host &host_info, const info::VHostAppName &vhost_app_name,
							const cfg::vhost::app::Application &app_config)
			: info::Application(host_info, 1, vhost_app_name, app_config, false)
		{
		}
	};

	// A stream that runs `on_stop` from its `StopStream()`.
	// It starts out playing, so the motor thread only asks it for media and never stops it.
	class StubPullStream : public pvd::PullStream
	{
	public:
		StubPullStream(const std::shared_ptr<pvd::Application> &application, const info::Stream &stream_info,
					   const std::vector<ov::String> &url_list, const std::shared_ptr<pvd::PullStreamProperties> &properties)
			: PullStream(application, stream_info, url_list, properties)
		{
			SetState(State::PLAYING);
		}

		ProcessMediaEventTrigger GetProcessMediaEventTriggerMode() override
		{
			return ProcessMediaEventTrigger::TRIGGER_INTERVAL;
		}

		std::shared_ptr<ov::Socket> GetSocketForDetectingEvent() override
		{
			return nullptr;
		}

		ProcessMediaResult ProcessMediaPacket() override
		{
			return ProcessMediaResult::PROCESS_MEDIA_TRY_AGAIN;
		}

		std::function<void()> on_stop = nullptr;

	protected:
		bool StartStream(const std::shared_ptr<const ov::Url> &url) override
		{
			return true;
		}

		bool RestartStream(const std::shared_ptr<const ov::Url> &url) override
		{
			return true;
		}

		bool StopStream() override
		{
			if (on_stop != nullptr)
			{
				on_stop();
			}

			return true;
		}
	};

	class StubPullApplication : public pvd::PullApplication
	{
	public:
		StubPullApplication(const std::shared_ptr<pvd::PullProvider> &provider, const info::Application &application_info)
			: PullApplication(provider, application_info)
		{
		}

		using PullApplication::CreateStream;

	protected:
		std::shared_ptr<pvd::PullStream> CreateStream(const uint32_t stream_id, const ov::String &stream_name,
													  const std::vector<ov::String> &url_list,
													  const std::shared_ptr<pvd::PullStreamProperties> &properties) override
		{
			info::Stream stream_info(*this, StreamSourceType::Ovt);
			stream_info.SetId(stream_id);
			stream_info.SetName(stream_name);

			return std::make_shared<StubPullStream>(GetSharedPtrAs<pvd::Application>(), stream_info, url_list, properties);
		}
	};

	// Holds a pull application built from a minimal virtual host, which has not been started
	struct Fixture
	{
		bool CreateApplication()
		{
			char path[]	 = "/tmp/ome_pull_application_test_XXXXXX";
			const int fd = ::mkstemp(path);
			if (fd < 0)
			{
				return false;
			}
			::close(fd);

			{
				std::ofstream out(path);
				out << "<VirtualHost>"
					<< "<Name>default</Name>"
					<< "<Host><Names><Name>*</Name></Names></Host>"
					<< "<Applications><Application>"
					<< "<Name>app</Name><Type>live</Type>"
					<< "<Providers><OVT /></Providers>"
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
				return false;
			}

			::remove(path);

			const auto app_list = vhost_config.GetApplicationList();
			if (app_list.empty())
			{
				return false;
			}

			router			 = std::make_shared<StubMediaRouter>();
			provider		 = std::make_shared<StubPullProvider>(server_config, router);
			host_info		 = std::make_shared<info::Host>("test-server", "test-server-id", vhost_config);
			application_info = std::make_shared<TestApplicationInfo>(*host_info, info::VHostAppName("default", "app"), app_list[0]);
			application		 = std::make_shared<StubPullApplication>(provider, *application_info);

			return true;
		}

		std::shared_ptr<StubPullStream> CreateStream(const char *name)
		{
			auto properties = std::make_shared<pvd::PullStreamProperties>();
			auto stream		= application->CreateStream(name, {"ovt://127.0.0.1:1/app/stream"}, properties);

			return std::dynamic_pointer_cast<StubPullStream>(stream);
		}

		cfg::Server server_config;
		std::shared_ptr<StubMediaRouter> router;
		std::shared_ptr<StubPullProvider> provider;
		std::shared_ptr<info::Host> host_info;
		std::shared_ptr<TestApplicationInfo> application_info;
		std::shared_ptr<StubPullApplication> application;
	};
}  // namespace

// A stream deleted while `DeleteAllStreams()` stops the motors it took is deleted once and logs no error.
// The deletion runs from inside the first `StopStream()`,
// before `DeleteAllStreams()` removes the streams from the application.
TEST(PullApplicationDeletion, DeletingAStreamWhileTheMotorsStopLogsNoError)
{
	Fixture fixture;
	ASSERT_TRUE(fixture.CreateApplication());

	auto first	= fixture.CreateStream("first");
	auto second = fixture.CreateStream("second");
	ASSERT_NE(first, nullptr);
	ASSERT_NE(second, nullptr);

	auto application = fixture.application;
	int deletions	 = 0;
	bool deleted	 = false;

	// `StreamMotor::Stop()` stops its streams on the thread that calls `DeleteAllStreams()`,
	// so only that thread runs these.
	auto delete_once = [&](const std::shared_ptr<StubPullStream> &other) {
		if (deletions++ == 0)
		{
			deleted = application->DeleteStream(other);
		}
	};
	first->on_stop	= [&, other = std::weak_ptr<StubPullStream>(second)]() { delete_once(other.lock()); };
	second->on_stop = [&, other = std::weak_ptr<StubPullStream>(first)]() { delete_once(other.lock()); };

	testing::internal::CaptureStderr();
	EXPECT_TRUE(application->DeleteAllStreams());
	const std::string log = testing::internal::GetCapturedStderr();

	first->on_stop	= nullptr;
	second->on_stop = nullptr;

	EXPECT_GE(deletions, 1);
	EXPECT_TRUE(deleted);
	EXPECT_TRUE(application->GetStreams().empty());
	EXPECT_EQ(log.find("Could not find StreamMotor"), std::string::npos) << log;
	EXPECT_EQ(log.find("Could not find stream to be removed"), std::string::npos) << log;
}
