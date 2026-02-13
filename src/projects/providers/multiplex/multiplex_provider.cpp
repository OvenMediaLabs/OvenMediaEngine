//==============================================================================
//
//  MultiplexProvider
//
//  Created by Getroot
//  Copyright (c) 2023 AirenSoft. All rights reserved.
//
//==============================================================================

#include "multiplex_provider.h"
#include "multiplex_application.h"

namespace pvd
{
	std::shared_ptr<MultiplexProvider> MultiplexProvider::Create(const cfg::Server &server_config, const std::shared_ptr<MediaRouterInterface> &router)
	{
		auto provider = std::make_shared<MultiplexProvider>(server_config, router);
		if (!provider->Start())
		{
			return nullptr;
		}

		return provider;
	}

	MultiplexProvider::MultiplexProvider(const cfg::Server &server_config, const std::shared_ptr<MediaRouterInterface> &router)
		: Provider(server_config, router)
	{
	}

	MultiplexProvider::~MultiplexProvider()
	{
	}

	bool MultiplexProvider::Start()
	{
		_multiplex_watcher_timer.Push([this](void *parameter) -> ov::DelayQueueAction {	
			auto apps = GetApplications();
			for (auto &item : apps)
			{
				auto app = item.second;
				auto multiplex_app = std::dynamic_pointer_cast<MultiplexApplication>(app);
				if (multiplex_app)
				{
					multiplex_app->OnMultiplexWatcherTick();
				}
			}
			return ov::DelayQueueAction::Repeat;
		}, 1000);
		_multiplex_watcher_timer.Start();

		return Provider::Start();
	}

	bool MultiplexProvider::Stop()
	{
		_multiplex_watcher_timer.Stop();

		return Provider::Stop();
	}

	bool MultiplexProvider::OnCreateHost(const info::Host &host_info)
	{
		// Nothing to do
		return true;
	}

	bool MultiplexProvider::OnDeleteHost(const info::Host &host_info)
	{
		// Nothing to do
		return true;
	}

	std::shared_ptr<Application> MultiplexProvider::OnCreateProviderApplication(const info::Application &application_info)
	{
		auto application = MultiplexApplication::Create(GetSharedPtrAs<Provider>(), application_info);
		if (application == nullptr)
		{
			return nullptr;
		}

		return application;
	}

	bool MultiplexProvider::OnDeleteProviderApplication(const std::shared_ptr<Application> &application)
	{
		return true;
	}
}