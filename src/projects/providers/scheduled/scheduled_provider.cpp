//==============================================================================
//
//  ScheduledProvider
//
//  Created by Getroot
//  Copyright (c) 2023 AirenSoft. All rights reserved.
//
//==============================================================================

#include "scheduled_provider.h"
#include "scheduled_application.h"

namespace pvd
{
	std::shared_ptr<ScheduledProvider> ScheduledProvider::Create(const cfg::Server &server_config, const std::shared_ptr<MediaRouterInterface> &router)
	{
		auto provider = std::make_shared<ScheduledProvider>(server_config, router);
		if (!provider->Start())
		{
			return nullptr;
		}

		return provider;
	}

	ScheduledProvider::ScheduledProvider(const cfg::Server &server_config, const std::shared_ptr<MediaRouterInterface> &router)
		: Provider(server_config, router)
	{
	}

	ScheduledProvider::~ScheduledProvider()
	{
	}

	bool ScheduledProvider::Start()
	{
		_schedule_watcher_timer.Push([this](void *parameter) -> ov::DelayQueueAction {	
			auto apps = GetApplications();
			for (auto &item : apps)
			{
				auto app = item.second;
				auto scheduled_app = std::dynamic_pointer_cast<ScheduledApplication>(app);
				if (scheduled_app)
				{
					scheduled_app->OnScheduleWatcherTick();
				}
			}
			return ov::DelayQueueAction::Repeat;
		}, 1000);
		_schedule_watcher_timer.Start();

		return Provider::Start();
	}

	bool ScheduledProvider::Stop()
	{
		_schedule_watcher_timer.Stop();

		return Provider::Stop();
	}

	bool ScheduledProvider::OnCreateHost(const info::Host &host_info)
	{
		// Nothing to do
		return true;
	}

	bool ScheduledProvider::OnDeleteHost(const info::Host &host_info)
	{
		// Nothing to do
		return true;
	}

	std::shared_ptr<Application> ScheduledProvider::OnCreateProviderApplication(const info::Application &application_info)
	{
		auto application = ScheduledApplication::Create(GetSharedPtrAs<Provider>(), application_info);
		if (application == nullptr)
		{
			return nullptr;
		}

		return application;
	}

	bool ScheduledProvider::OnDeleteProviderApplication(const std::shared_ptr<Application> &application)
	{
		return true;
	}
}