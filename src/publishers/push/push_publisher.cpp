//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Keukhan
//  Copyright (c) 2024 AirenSoft. All rights reserved.
//
//==============================================================================

#include "push_publisher.h"

#include "push_application.h"
#include "push_private.h"

namespace pub
{
	std::shared_ptr<PushPublisher> PushPublisher::Create(const cfg::Server &server_config, const std::shared_ptr<MediaRouterInterface> &router)
	{
		auto obj = std::make_shared<PushPublisher>(server_config, router);

		if (!obj->Start())
		{
			logte("An error occurred while creating PushPublisher");
			return nullptr;
		}

		return obj;
	}

	PushPublisher::PushPublisher(const cfg::Server &server_config, const std::shared_ptr<MediaRouterInterface> &router)
		: Publisher(server_config, router)
	{
		logtt("PushPublisher has been create");
	}

	PushPublisher::~PushPublisher()
	{
		logtt("PushPublisher has been terminated finally");
	}

	bool PushPublisher::Start()
	{
		return Publisher::Start();
	}

	bool PushPublisher::Stop()
	{
		return Publisher::Stop();
	}

	bool PushPublisher::OnCreateHost(const info::Host &host_info)
	{
		return true;
	}

	bool PushPublisher::OnDeleteHost(const info::Host &host_info)
	{
		return true;
	}

	std::shared_ptr<pub::Application> PushPublisher::OnCreatePublisherApplication(const info::Application &application_info)
	{
		if (IsModuleAvailable() == false)
		{
			return nullptr;
		}

		return PushApplication::Create(PushPublisher::GetSharedPtrAs<pub::Publisher>(), application_info);
	}

	bool PushPublisher::OnDeletePublisherApplication(const std::shared_ptr<pub::Application> &application)
	{
		return true;
	}
}  // namespace pub